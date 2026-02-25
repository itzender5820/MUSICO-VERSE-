#include "playlist.h"
#include <cctype>
#include <algorithm>

bool Playlist::is_audio(const fs::path& p) {
    std::string ext = p.extension().string();
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".flac" || ext == ".mp3"  || ext == ".m4a"  ||
           ext == ".opus" || ext == ".ogg"  || ext == ".wav"  ||
           ext == ".aac"  || ext == ".webm" || ext == ".wma"  ||
           ext == ".ape"  || ext == ".mka"  || ext == ".mp4";
}

void Playlist::load_dir(const std::string& dir) {
    entries_.clear();
    idx_ = 0;
    std::error_code ec;
    // Recursive scan — finds songs inside sub-folders
    for (auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec) && is_audio(entry.path())) {
            add(entry.path().string());
        }
    }
    std::sort(entries_.begin(), entries_.end(),
        [](const PlaylistEntry& a, const PlaylistEntry& b){
            // Case-insensitive sort by display name (filename)
            std::string la = a.display_name, lb = b.display_name;
            std::transform(la.begin(),la.end(),la.begin(),::tolower);
            std::transform(lb.begin(),lb.end(),lb.begin(),::tolower);
            return la < lb;
        });
}

void Playlist::import(const std::string& path) {
    std::error_code ec;
    if (fs::is_directory(path, ec))       load_dir(path);
    else if (fs::is_regular_file(path, ec) && is_audio(path)) add(path);
}

void Playlist::add(const std::string& path) {
    entries_.push_back({ path, make_display(path) });
}

const PlaylistEntry* Playlist::current() const {
    if (entries_.empty()) return nullptr;
    return &entries_[idx_];
}
const PlaylistEntry* Playlist::next() {
    if (entries_.empty()) return nullptr;
    if (loop) return &entries_[idx_];
    idx_ = (idx_ + 1) % (int)entries_.size();
    return &entries_[idx_];
}
const PlaylistEntry* Playlist::prev() {
    if (entries_.empty()) return nullptr;
    idx_ = (idx_ - 1 + (int)entries_.size()) % (int)entries_.size();
    return &entries_[idx_];
}
void Playlist::select(int idx) {
    if (idx >= 0 && idx < (int)entries_.size()) idx_ = idx;
}
