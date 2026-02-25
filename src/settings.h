#pragma once
#include <string>
#include <fstream>
#include <map>
#include <algorithm>
#include <filesystem>
#include <cmath>

namespace fs = std::filesystem;

// ─── Visualizer styles ───────────────────────────────────────────────────────
enum class VizStyle { BARS=0, MIRROR=1, WAVE=2, FIRE=3, DOTS=4 };
static const char* VIZ_STYLE_NAMES[] = {
    "BARS (braille)", "MIRROR (NCS)", "WAVE (sine)", "FIRE (block)", "DOTS (peaks)"
};
static const int VIZ_STYLE_COUNT = 5;

// ─── A single color = ANSI-256 code + brightness 0-100 ───────────────────────
struct Color {
    int ansi       = 255;  // ANSI 256 palette index  (0-255)
    int brightness = 100;  // 0 = black, 100 = full color

    Color() = default;
    Color(int a, int b) : ansi(a), brightness(b) {}
};

// ─── Default color scheme ────────────────────────────────────────────────────
struct ColorScheme {
    Color viz          = {51,  100};  // bright cyan
    Color border       = {39,  100};  // dodger blue
    Color title        = {231, 100};  // pure white
    Color meta_key     = {153, 100};  // sky blue
    Color meta_val     = {123, 100};  // aqua
    Color progress     = {82,  100};  // lime green
    Color playlist     = {253, 100};  // near-white
    Color pl_active_fg = {231, 100};  // white text on active
    Color pl_active_bg = {27,  100};  // deep blue bg
    Color lyr_dim      = {253, 60};   // dimmed white for non-active lines
    Color lyr_hi       = {226, 100};  // bright yellow active lyric
    Color noise        = {48,  80};   // spring green
    Color status       = {252, 80};   // light grey
};

// ─── ANSI-256 → RGB (0-1000 scale for ncurses) ───────────────────────────────
inline static void ansi256_to_rgb1000(int code, short& r, short& g, short& b) {
    code = std::clamp(code, 0, 255);
    if (code < 16) {
        // Standard 16 — use proper sRGB values
        static const short T[16][3] = {
            {0,0,0},       {800,0,0},     {0,800,0},     {800,500,0},
            {0,0,800},     {800,0,800},   {0,800,800},   {753,753,753},
            {500,500,500}, {1000,333,333},{333,1000,333},{1000,1000,333},
            {333,333,1000},{1000,333,1000},{333,1000,1000},{1000,1000,1000}
        };
        r=T[code][0]; g=T[code][1]; b=T[code][2];
    } else if (code < 232) {
        int i=code-16, bv=i%6; i/=6; int gv=i%6; i/=6; int rv=i%6;
        // Correction: 0→0, 1→95, 2→135 ... 5→255 → scale to 1000
        auto ch1000=[](int v)->short{
            static const short lut[]={0,373,529,686,843,1000};
            return lut[v];
        };
        r=ch1000(rv); g=ch1000(gv); b=ch1000(bv);
    } else {
        // Greyscale 232-255: 8,18,28...238
        short v = (short)(31 + 39*(code-232)); // 0-1000 scale
        r=g=b=v;
    }
}

// Apply brightness multiplier (0-100) to RGB values
inline static void apply_brightness(short& r, short& g, short& b, int brightness) {
    float f = std::clamp(brightness, 0, 100) / 100.0f;
    r = (short)(r * f);
    g = (short)(g * f);
    b = (short)(b * f);
}

// ─── Settings ────────────────────────────────────────────────────────────────
struct Settings {
    static constexpr int VERSION = 110;  // 1.1.0
    VizStyle    viz_style = VizStyle::BARS;
    ColorScheme colors;

    static std::string config_path() {
        const char* home = getenv("HOME");
        if (!home) home = "/data/data/com.termux/files/home";
        return std::string(home) + "/.config/musico_verse/settings.conf";
    }

    void save() const {
        std::string path = config_path();
        fs::create_directories(fs::path(path).parent_path());
        std::ofstream f(path);
        if (!f.is_open()) return;

        auto wc = [&](const char* k, const Color& c) {
            f << k << "=" << c.ansi << "," << c.brightness << "\n";
        };
        f << "version="    << VERSION       << "\n";
        f << "viz_style="  << (int)viz_style<< "\n";
        wc("viz",          colors.viz);
        wc("border",       colors.border);
        wc("title",        colors.title);
        wc("meta_key",     colors.meta_key);
        wc("meta_val",     colors.meta_val);
        wc("progress",     colors.progress);
        wc("playlist",     colors.playlist);
        wc("pl_active_fg", colors.pl_active_fg);
        wc("pl_active_bg", colors.pl_active_bg);
        wc("lyr_dim",      colors.lyr_dim);
        wc("lyr_hi",       colors.lyr_hi);
        wc("noise",        colors.noise);
        wc("status",       colors.status);
    }

    void load() {
        std::ifstream f(config_path());
        if (!f.is_open()) return;
        std::map<std::string,std::string> kv;
        std::string line;
        while (std::getline(f, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            kv[line.substr(0,eq)] = line.substr(eq+1);
        }
        auto gi = [&](const char* k, int def) -> int {
            auto it=kv.find(k); return it!=kv.end() ? std::stoi(it->second) : def;
        };
        auto gc = [&](const char* k, Color def) -> Color {
            auto it=kv.find(k);
            if (it==kv.end()) return def;
            auto comma=it->second.find(',');
            if (comma==std::string::npos) return def;
            try {
                return Color(std::stoi(it->second.substr(0,comma)),
                             std::stoi(it->second.substr(comma+1)));
            } catch(...) { return def; }
        };
        if (gi("version",0) < VERSION) return; // old file → use bright defaults
        viz_style          = (VizStyle)std::clamp(gi("viz_style",0),0,VIZ_STYLE_COUNT-1);
        colors.viz         = gc("viz",         colors.viz);
        colors.border      = gc("border",      colors.border);
        colors.title       = gc("title",       colors.title);
        colors.meta_key    = gc("meta_key",    colors.meta_key);
        colors.meta_val    = gc("meta_val",    colors.meta_val);
        colors.progress    = gc("progress",    colors.progress);
        colors.playlist    = gc("playlist",    colors.playlist);
        colors.pl_active_fg= gc("pl_active_fg",colors.pl_active_fg);
        colors.pl_active_bg= gc("pl_active_bg",colors.pl_active_bg);
        colors.lyr_dim     = gc("lyr_dim",     colors.lyr_dim);
        colors.lyr_hi      = gc("lyr_hi",      colors.lyr_hi);
        colors.noise       = gc("noise",       colors.noise);
        colors.status      = gc("status",      colors.status);
    }
};
