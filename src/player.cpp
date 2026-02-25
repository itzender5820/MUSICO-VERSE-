#include "player.h"
#include <algorithm>
#include <cmath>

Player::Player() = default;
Player::~Player() { shutdown(); }

bool Player::init() {
    AudioSpec spec;
    spec.sample_rate = 44100;
    spec.channels    = 2;
    spec.buffer_size = 2048;

    if (!audio_.init(spec)) {
        status_msg = "AudioEngine init failed";
        return false;
    }
    audio_.set_volume(volume_ / 100.f);

    // Load playlist recursively from music dir
    playlist_.load_dir(Playlist::MUSIC_DIR);
    return true;
}

void Player::shutdown() {
    quit_ = true;
    audio_.stop();
    audio_.shutdown();
    decoder_.close();
}

void Player::load_and_play(const std::string& path) {
    audio_.stop();
    decoder_.close();
    track_done_ = false;

    if (!decoder_.open(path)) {
        status_msg = "Failed to open: " + path;
        state_     = PlayerState::STOPPED;
        return;
    }
    meta_  = decoder_.meta();
    state_ = PlayerState::PLAYING;

    audio_.start([this](float* buf, int frames) -> int {
        return this->audio_callback(buf, frames);
    });
}

int Player::audio_callback(float* buf, int frames) {
    if (state_ != PlayerState::PLAYING) {
        std::fill(buf, buf + frames * 2, 0.f);
        return frames;
    }

    int written = decoder_.decode_next(buf, frames);

    if (written > 0) {
        visualizer_.push_samples(buf, written,
            (int)meta_.channels, (int)meta_.sample_rate);
    }

    if (written < frames) {
        std::fill(buf + written * 2, buf + frames * 2, 0.f);
        track_done_ = true;
    }
    return frames;
}

void Player::play_pause() {
    if (state_ == PlayerState::PLAYING) {
        state_ = PlayerState::PAUSED;
        audio_.pause();
    } else if (state_ == PlayerState::PAUSED) {
        state_ = PlayerState::PLAYING;
        audio_.resume();
    } else {
        auto* entry = playlist_.current();
        if (entry) load_and_play(entry->path);
    }
}

void Player::stop()  { audio_.stop(); state_ = PlayerState::STOPPED; }
void Player::next()  { auto* e = playlist_.next(); if (e) load_and_play(e->path); }
void Player::prev()  { auto* e = playlist_.prev(); if (e) load_and_play(e->path); }
void Player::seek(double s) { decoder_.seek(s); }

void Player::set_volume(int delta) {
    volume_ = std::clamp(volume_ + delta * 5, 0, 300);
    audio_.set_volume(volume_ / 100.f);
}
void Player::set_speed(int delta) {
    float sp = audio_.get_speed() + delta * 0.1f;
    audio_.set_speed(std::clamp(sp, 0.5f, 2.0f));
}

void Player::toggle_repeat() { playlist_.repeat = !playlist_.repeat; }
void Player::toggle_loop()   { playlist_.loop   = !playlist_.loop;   }
double Player::position() const { return decoder_.position(); }
double Player::duration() const { return (double)meta_.duration_sec; }
