#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

struct PlaylistEntry {
    std::string path;
    std::string display_name;
};

class Playlist {
public:
    inline static const std::string MUSIC_DIR = "/sdcard/music/";

    // Recursively scan dir for all supported audio files
    void load_dir(const std::string& dir = MUSIC_DIR);
    void import(const std::string& path);
    void add(const std::string& path);

    const PlaylistEntry* current() const;
    const PlaylistEntry* next();
    const PlaylistEntry* prev();
    void  select(int idx);
    int   current_idx() const { return idx_; }
    int   count()       const { return (int)entries_.size(); }

    bool repeat = false;
    bool loop   = false;

    const std::vector<PlaylistEntry>& entries() const { return entries_; }

private:
    std::vector<PlaylistEntry> entries_;
    int idx_ = 0;

    static bool is_audio(const fs::path& p);
    static std::string make_display(const std::string& path) {
        return fs::path(path).filename().string();
    }
};
