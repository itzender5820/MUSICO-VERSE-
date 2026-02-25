#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <cstdint>

struct TrackMeta {
    std::string title;
    std::string artist;
    std::string album;
    std::string format;
    int         duration_sec = 0;
    int         year         = 0;
    uint32_t    sample_rate  = 44100;
    uint32_t    channels     = 2;
    uint32_t    bps          = 16;
    std::string cover_art_ascii;
};

class AvDecoder {
public:
    AvDecoder();
    ~AvDecoder();

    // Open any format FFmpeg supports (flac, mp3, m4a, opus, ogg, wav…)
    bool open(const std::string& path);
    void close();

    // Decode up to max_frames of interleaved float PCM into out[].
    // Returns frames written; 0 = EOF; -1 = error.
    int decode_next(float* out, int max_frames);

    bool seek(double seconds);
    double position() const;

    const TrackMeta& meta() const { return meta_; }
    bool is_open() const { return open_; }
    bool is_eof()  const { return eof_.load(); }

private:
    struct Impl;
    Impl* impl_ = nullptr;

    TrackMeta meta_;
    std::atomic<bool> eof_{false};
    bool open_ = false;

    // Leftover decoded samples from last AVFrame not yet consumed
    std::vector<float> leftover_;
    int    leftover_pos_ = 0;
    double last_pos_sec_ = 0.0;   // cached position — updated before av_frame_unref
};
