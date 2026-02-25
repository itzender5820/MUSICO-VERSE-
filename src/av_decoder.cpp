#include "av_decoder.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cstdio>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

// ─── Internal state ──────────────────────────────────────────────────────────
struct AvDecoder::Impl {
    AVFormatContext* fmt_ctx   = nullptr;
    AVCodecContext*  codec_ctx = nullptr;
    SwrContext*      swr_ctx   = nullptr;
    AVPacket*        pkt       = nullptr;
    AVFrame*         frame     = nullptr;
    int              stream_idx = -1;
};

// ─── Constructor / Destructor ────────────────────────────────────────────────
AvDecoder::AvDecoder()  { impl_ = new Impl(); }
AvDecoder::~AvDecoder() { close(); delete impl_; }

// ─── open ────────────────────────────────────────────────────────────────────
bool AvDecoder::open(const std::string& path) {
    close();

    // Silence FFmpeg's internal log spam (seek warnings, skipped samples, etc.)
    av_log_set_level(AV_LOG_FATAL);

    // Open container
    if (avformat_open_input(&impl_->fmt_ctx, path.c_str(), nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(impl_->fmt_ctx, nullptr) < 0) {
        avformat_close_input(&impl_->fmt_ctx);
        return false;
    }

    // Find best audio stream
    impl_->stream_idx = av_find_best_stream(
        impl_->fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (impl_->stream_idx < 0) {
        avformat_close_input(&impl_->fmt_ctx);
        return false;
    }

    AVStream* st = impl_->fmt_ctx->streams[impl_->stream_idx];

    // Find & open codec
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) { avformat_close_input(&impl_->fmt_ctx); return false; }

    impl_->codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(impl_->codec_ctx, st->codecpar);
    if (avcodec_open2(impl_->codec_ctx, codec, nullptr) < 0) {
        avcodec_free_context(&impl_->codec_ctx);
        avformat_close_input(&impl_->fmt_ctx);
        return false;
    }

    // Build SwrContext: decode → interleaved float 44100 stereo
    uint32_t out_rate  = 44100;
    uint32_t out_ch    = 2;

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
    // New channel-layout API (FFmpeg ≥ 5.1)
    AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(&impl_->swr_ctx,
        &out_layout,     AV_SAMPLE_FMT_FLTP, (int)out_rate,
        &impl_->codec_ctx->ch_layout, impl_->codec_ctx->sample_fmt,
        impl_->codec_ctx->sample_rate, 0, nullptr);
#else
    impl_->swr_ctx = swr_alloc_set_opts(nullptr,
        AV_CH_LAYOUT_STEREO,          AV_SAMPLE_FMT_FLTP, (int)out_rate,
        impl_->codec_ctx->channel_layout
            ? impl_->codec_ctx->channel_layout
            : av_get_default_channel_layout(impl_->codec_ctx->channels),
        impl_->codec_ctx->sample_fmt,
        impl_->codec_ctx->sample_rate, 0, nullptr);
#endif
    swr_init(impl_->swr_ctx);

    impl_->pkt   = av_packet_alloc();
    impl_->frame = av_frame_alloc();

    // ── Metadata ─────────────────────────────────────────────────────────────
    auto tag = [&](const char* key) -> std::string {
        AVDictionaryEntry* e = av_dict_get(impl_->fmt_ctx->metadata, key, nullptr, AV_DICT_IGNORE_SUFFIX);
        if (!e) e = av_dict_get(st->metadata, key, nullptr, AV_DICT_IGNORE_SUFFIX);
        return e ? e->value : "";
    };

    meta_.title   = tag("title");
    meta_.artist  = tag("artist");
    meta_.album   = tag("album");
    std::string yr = tag("date");
    if (yr.size() >= 4) {
        try { meta_.year = std::stoi(yr.substr(0, 4)); } catch (...) {}
    }

    // Duration
    if (impl_->fmt_ctx->duration != AV_NOPTS_VALUE)
        meta_.duration_sec = (int)(impl_->fmt_ctx->duration / AV_TIME_BASE);

    // Format name
    meta_.format = impl_->fmt_ctx->iformat->name;
    // Simplify format string (e.g. "mp3" from "mp3,mp2,mp2float")
    if (meta_.format.size() > 6) meta_.format = meta_.format.substr(0, 6);

    // Sample info
    meta_.sample_rate = out_rate;
    meta_.channels    = out_ch;
    meta_.bps         = 32;

    // Fallback title from filename
    if (meta_.title.empty()) {
        size_t s = path.rfind('/');
        size_t d = path.rfind('.');
        meta_.title = path.substr(s + 1, (d != std::string::npos ? d : path.size()) - s - 1);
    }

    eof_          = false;
    open_         = true;
    leftover_.clear();
    leftover_pos_ = 0;
    last_pos_sec_ = 0.0;
    return true;
}

// ─── close ───────────────────────────────────────────────────────────────────
void AvDecoder::close() {
    if (impl_->frame)    { av_frame_free(&impl_->frame);   impl_->frame = nullptr; }
    if (impl_->pkt)      { av_packet_free(&impl_->pkt);    impl_->pkt   = nullptr; }
    if (impl_->swr_ctx)  { swr_free(&impl_->swr_ctx);      impl_->swr_ctx = nullptr; }
    if (impl_->codec_ctx){ avcodec_free_context(&impl_->codec_ctx); impl_->codec_ctx = nullptr; }
    if (impl_->fmt_ctx)  { avformat_close_input(&impl_->fmt_ctx); impl_->fmt_ctx = nullptr; }
    impl_->stream_idx = -1;
    eof_  = false;
    open_ = false;
    leftover_.clear();
    leftover_pos_ = 0;
    last_pos_sec_ = 0.0;
}

// ─── decode_next ─────────────────────────────────────────────────────────────
// Returns interleaved float stereo PCM frames.
int AvDecoder::decode_next(float* out, int max_frames) {
    if (!open_ || eof_) return 0;

    const int CH = 2;
    int written = 0;

    while (written < max_frames) {
        // 1. Drain leftover from previous AVFrame
        int avail = (int)leftover_.size() / CH - leftover_pos_;
        if (avail > 0) {
            int take = std::min(avail, max_frames - written);
            memcpy(out + written * CH,
                   leftover_.data() + leftover_pos_ * CH,
                   take * CH * sizeof(float));
            written      += take;
            leftover_pos_ += take;
            if (leftover_pos_ >= (int)leftover_.size() / CH) {
                leftover_.clear();
                leftover_pos_ = 0;
            }
            continue;
        }

        // 2. Send next packet to codec
        int ret = av_read_frame(impl_->fmt_ctx, impl_->pkt);
        if (ret == AVERROR_EOF) {
            // Flush decoder
            avcodec_send_packet(impl_->codec_ctx, nullptr);
        } else if (ret < 0) {
            eof_ = true; break;
        } else {
            if (impl_->pkt->stream_index != impl_->stream_idx) {
                av_packet_unref(impl_->pkt);
                continue;
            }
            avcodec_send_packet(impl_->codec_ctx, impl_->pkt);
            av_packet_unref(impl_->pkt);
        }

        // 3. Receive decoded frame
        ret = avcodec_receive_frame(impl_->codec_ctx, impl_->frame);
        if (ret == AVERROR(EAGAIN)) continue;
        if (ret == AVERROR_EOF)     { eof_ = true; break; }
        if (ret < 0)                { eof_ = true; break; }

        // 4. Resample to interleaved float stereo
        int out_samples = (int)av_rescale_rnd(
            swr_get_delay(impl_->swr_ctx, impl_->codec_ctx->sample_rate) + impl_->frame->nb_samples,
            44100, impl_->codec_ctx->sample_rate, AV_ROUND_UP);

        // Planar float output buffers for swr
        std::vector<float> pl(out_samples * CH);
        float* planes[2] = { pl.data(), pl.data() + out_samples };
        uint8_t* out_planes[2] = {
            reinterpret_cast<uint8_t*>(planes[0]),
            reinterpret_cast<uint8_t*>(planes[1])
        };

        int got = swr_convert(impl_->swr_ctx,
            out_planes, out_samples,
            (const uint8_t**)impl_->frame->data, impl_->frame->nb_samples);
        if (got <= 0) continue;

        // Interleave L/R planes → leftover_
        leftover_.resize(got * CH);
        for (int i = 0; i < got; ++i) {
            leftover_[i * 2]     = planes[0][i];
            leftover_[i * 2 + 1] = planes[1][i];
        }
        leftover_pos_ = 0;
        // Cache position NOW — before av_frame_unref wipes the frame
        {
            AVStream* st  = impl_->fmt_ctx->streams[impl_->stream_idx];
            int64_t   pts = impl_->frame->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE) pts = impl_->frame->pts;
            if (pts != AV_NOPTS_VALUE)
                last_pos_sec_ = pts * av_q2d(st->time_base);
        }
        av_frame_unref(impl_->frame);
    }
    return written;
}

// ─── seek ────────────────────────────────────────────────────────────────────
bool AvDecoder::seek(double seconds) {
    if (!open_) return false;
    int64_t ts = (int64_t)(seconds * AV_TIME_BASE);
    int ret = av_seek_frame(impl_->fmt_ctx, -1, ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) return false;
    avcodec_flush_buffers(impl_->codec_ctx);
    leftover_.clear();
    leftover_pos_ = 0;
    last_pos_sec_ = seconds;
    eof_ = false;
    return true;
}

// ─── position ────────────────────────────────────────────────────────────────
double AvDecoder::position() const {
    return last_pos_sec_;
}
