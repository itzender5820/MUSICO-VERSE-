#pragma once
#include "audio_engine.h"
#include "av_decoder.h"
#include "playlist.h"
#include "visualizer.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <string>

enum class PlayerState { STOPPED, PLAYING, PAUSED };

class Player {
public:
    Player();
    ~Player();

    bool init();
    void shutdown();

    // Commands
    void load_and_play(const std::string& path);
    void play_pause();
    void stop();
    void next();
    void prev();
    void seek(double secs);
    void set_volume(int delta);   // +1 / -1  (0-100 range)
    void set_speed(int delta);    // +1 / -1  (50-200 %)
    void toggle_repeat();
    void toggle_loop();

    // State queries
    PlayerState state() const { return state_.load(); }
    double position()   const;
    double duration()   const;
    int    volume()     const { return volume_; }
    int    speed_pct()  const { return (int)(audio_.get_speed() * 100); }
    bool   is_repeat()  const { return playlist_.repeat; }
    bool   is_loop()    const { return playlist_.loop; }

    const TrackMeta& current_meta() const { return meta_; }
    Playlist&  playlist()   { return playlist_; }
    Visualizer& visualizer() { return visualizer_; }

    std::string status_msg;

private:
    AudioEngine  audio_;
    AvDecoder    decoder_;
    Playlist     playlist_;
    Visualizer   visualizer_;
    TrackMeta    meta_;

    std::atomic<PlayerState> state_{PlayerState::STOPPED};
    int volume_ = 80;
    std::atomic<bool> quit_{false};
    std::atomic<bool> track_done_{false};

    int  audio_callback(float* buf, int frames);
    static const int DECODE_CHUNK = 4096;
};
