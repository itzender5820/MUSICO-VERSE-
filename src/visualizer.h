#pragma once
#include <vector>
#include <array>
#include <mutex>
#include "settings.h"

static constexpr int TOTAL_BANDS = 19;
static const wchar_t LEVEL_CHARS[5] = { L' ', L'\u28C0', L'\u28E4', L'\u28F6', L'\u28FF' };
// Block chars for FIRE style
static const uint32_t FIRE_CHARS[5] = { ' ', 0x2591, 0x2592, 0x2593, 0x2588 }; // ░▒▓█

struct VisualizerFrame {
    std::vector<std::vector<uint32_t>> rows;
    int width=0, height=0;
};

class Visualizer {
public:
    void init(int width, int height);
    void push_samples(const float* s, int frames, int channels, int sr);
    VisualizerFrame render(VizStyle style = VizStyle::BARS);

private:
    int width_=80, height_=15;
    static const int FFT_SIZE=2048;
    std::vector<float> ring_;
    int ring_write_=0, sample_rate_=44100;
    std::array<float,TOTAL_BANDS> smooth_{};
    std::mutex mtx_;

    void compute_bands_locked();

    VisualizerFrame render_bars(const std::array<float,TOTAL_BANDS>& snap);
    VisualizerFrame render_mirror(const std::array<float,TOTAL_BANDS>& snap);
    VisualizerFrame render_wave(const std::array<float,TOTAL_BANDS>& snap);
    VisualizerFrame render_fire(const std::array<float,TOTAL_BANDS>& snap);
    VisualizerFrame render_dots(const std::array<float,TOTAL_BANDS>& snap);

    // Shared: build per-column float heights via cosine-interpolation
    std::vector<float> interpolate_cols(const std::array<float,TOTAL_BANDS>& snap,
                                        float scale=1.f) const;
};
