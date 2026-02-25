#pragma once
#include <cstdint>
#include <functional>
#include <atomic>
#include <vector>
#include <mutex>

// Audio callback: called when more PCM data is needed
// Returns number of frames actually written (0 = end of stream)
using AudioCallback = std::function<int(float* buffer, int frames)>;

struct AudioSpec {
    int sample_rate = 44100;
    int channels    = 2;
    int buffer_size = 4096; // frames per buffer
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool init(const AudioSpec& spec);
    void shutdown();

    bool start(AudioCallback cb);
    void stop();
    void pause();
    void resume();

    void set_volume(float vol); // 0.0 - 1.0
    float get_volume() const { return volume_.load(); }

    void set_speed(float speed); // 0.5 - 2.0
    float get_speed() const { return speed_.load(); }

    bool is_playing() const { return playing_.load(); }

    // Drain the latest PCM chunk for visualization (interleaved float stereo)
    std::vector<float> get_viz_buffer();
    void push_viz_buffer(const float* data, int frames);

public:  // needs to be reachable from static OpenSL ES callback
#ifdef __ANDROID__
    void enqueue_buffer();
#endif

private:
#ifdef __ANDROID__
    void* engine_obj_   = nullptr;
    void* engine_itf_   = nullptr;
    void* output_mix_   = nullptr;
    void* player_obj_   = nullptr;
    void* player_itf_   = nullptr;
    void* volume_itf_   = nullptr;
    void* buffer_queue_ = nullptr;

    static void buffer_queue_callback(void* bq, void* ctx);

    int16_t* pcm_bufs_[2] = {nullptr, nullptr};
    int cur_buf_ = 0;
    int buf_frames_ = 0;
#else
    // Stub for non-Android: write to /dev/audio or stdout
#endif
    AudioSpec spec_;
    AudioCallback callback_;
    std::atomic<float> volume_{1.0f};
    std::atomic<float> speed_{1.0f};
    std::atomic<bool> playing_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> initialized_{false};

    std::mutex viz_mutex_;
    std::vector<float> viz_buf_;
    static const int VIZ_BUF_SIZE = 8192;
};
