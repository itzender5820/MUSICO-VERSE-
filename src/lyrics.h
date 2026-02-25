#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <filesystem>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <cstdlib>

namespace fs = std::filesystem;

struct LrcLine { double time_sec = 0.0; std::string text; };

class Lyrics {
public:
    enum class State { IDLE, FETCHING, FOUND, NOT_FOUND };

    void load(const std::string& audio_path,
              const std::string& title, const std::string& artist) {
        cancel_fetch();
        { std::lock_guard<std::mutex> lk(mtx_);
          lines_.clear(); has_ts_=false; state_=State::IDLE; status_=""; }

        if (try_sidecar(audio_path)) return;

        if (title.empty() && artist.empty()) {
            state_ = State::NOT_FOUND; return;
        }
        state_      = State::FETCHING;
        status_     = "Fetching lyrics…";
        cancel_     = false;
        cache_path_ = make_cache_path(audio_path);
        fetch_thread_ = std::thread([this,title,artist]{ fetch_async(title,artist); });
    }

    void cancel_fetch() {
        cancel_ = true;
        if (fetch_thread_.joinable()) fetch_thread_.join();
        cancel_ = false;
    }

    State       state()  const { return state_.load(); }
    std::string status() const { std::lock_guard<std::mutex> l(mtx_); return status_; }
    bool has_lyrics()    const { std::lock_guard<std::mutex> l(mtx_); return !lines_.empty(); }

    std::vector<std::string> visible(double pos, int window) const {
        std::lock_guard<std::mutex> l(mtx_);
        if (lines_.empty()) return {};
        int cur   = active_idx(pos);
        int half  = window/2;
        int start = std::max(0, cur-half);
        int end   = std::min((int)lines_.size(), start+window);
        if (end-start < window) start = std::max(0, end-window);
        std::vector<std::string> out;
        for (int i=start;i<end;++i) out.push_back(lines_[i].text);
        return out;
    }

    int active_in_visible(double pos, int window) const {
        std::lock_guard<std::mutex> l(mtx_);
        if (lines_.empty()) return 0;
        int cur   = active_idx(pos);
        int half  = window/2;
        int start = std::max(0, cur-half);
        return cur-start;
    }

private:
    std::vector<LrcLine> lines_;
    bool                 has_ts_ = false;
    std::atomic<State>   state_{State::IDLE};
    std::string          status_, cache_path_;
    std::thread          fetch_thread_;
    std::atomic<bool>    cancel_{false};
    mutable std::mutex   mtx_;

    int active_idx(double pos) const {
        int cur=0;
        if (has_ts_)
            for (int i=0;i<(int)lines_.size();++i)
                if (lines_[i].time_sec <= pos) cur=i;
        return cur;
    }

    static std::string make_cache_path(const std::string& p) {
        return fs::path(p).replace_extension(".lrc").string();
    }

    bool try_sidecar(const std::string& p) {
        for (auto ext : {".lrc",".txt",".lyrics"}) {
            fs::path c = fs::path(p); c.replace_extension(ext);
            if (fs::exists(c) && parse_file(c.string())) {
                state_=State::FOUND; status_="Lyrics loaded"; return true;
            }
        }
        return false;
    }

    bool parse_file(const std::string& path) {
        std::ifstream f(path); if (!f.is_open()) return false;
        std::regex lrc_re(R"(\[(\d+):(\d+)[\.:](\d+)\])");
        std::regex meta_re(R"(\[[a-zA-Z]+:)");
        std::vector<LrcLine> tmp; bool timed=false;
        std::string line;
        while (std::getline(f,line)) {
            if (!line.empty()&&line.back()=='\r') line.pop_back();
            if (line.empty()||std::regex_search(line,meta_re)) continue;
            std::smatch m;
            if (std::regex_search(line,m,lrc_re)) {
                double ts=std::stod(m[1])*60+std::stod(m[2])+std::stod(m[3])*0.01;
                std::string text=std::regex_replace(line.substr(line.rfind(']')+1),lrc_re,"");
                if (!text.empty()){tmp.push_back({ts,text}); timed=true;}
            } else { tmp.push_back({0.0,line}); }
        }
        if (tmp.empty()) return false;
        if (timed) std::sort(tmp.begin(),tmp.end(),[](auto&a,auto&b){return a.time_sec<b.time_sec;});
        std::lock_guard<std::mutex> lk(mtx_);
        lines_=std::move(tmp); has_ts_=timed;
        return true;
    }

    void fetch_async(const std::string& title, const std::string& artist) {
        // Check syncedlyrics installed
        if (system("python3 -c \"import syncedlyrics\" 2>/dev/null") != 0) {
            std::lock_guard<std::mutex> lk(mtx_);
            state_=State::NOT_FOUND;
            status_="Run: pip install syncedlyrics";
            return;
        }
        if (cancel_) return;

        // Escape single quotes in query
        auto esc=[](const std::string& s){
            std::string o; for(char c:s){if(c=='\''||c=='\\')o+='\\'; o+=c;} return o;
        };
        std::string q = esc(artist) + " - " + esc(title);
        std::string cmd="python3 -c \""
            "import syncedlyrics,sys\n"
            "r=syncedlyrics.search('"+q+"')\n"
            "sys.stdout.write(r if r else '')\n"
            "\" 2>/dev/null";

        std::string result;
        FILE* pipe=popen(cmd.c_str(),"r");
        if (pipe) {
            char buf[512];
            while (!cancel_ && fgets(buf,sizeof(buf),pipe)) result+=buf;
            pclose(pipe);
        }
        if (cancel_) return;

        if (result.size()<10 || result.find('[')==std::string::npos) {
            std::lock_guard<std::mutex> lk(mtx_);
            state_=State::NOT_FOUND; status_="No lyrics found"; return;
        }

        // Cache to disk
        if (!cache_path_.empty()) {
            std::ofstream cf(cache_path_);
            if (cf.is_open()) cf<<result;
        }

        // Parse
        std::istringstream ss(result);
        std::regex lrc_re(R"(\[(\d+):(\d+)[\.:](\d+)\])");
        std::regex meta_re(R"(\[[a-zA-Z]+:)");
        std::vector<LrcLine> tmp; bool timed=false;
        std::string line;
        while (std::getline(ss,line)) {
            if (!line.empty()&&line.back()=='\r') line.pop_back();
            if (line.empty()||std::regex_search(line,meta_re)) continue;
            std::smatch m;
            if (std::regex_search(line,m,lrc_re)) {
                double ts=std::stod(m[1])*60+std::stod(m[2])+std::stod(m[3])*0.01;
                std::string text=std::regex_replace(line.substr(line.rfind(']')+1),lrc_re,"");
                if (!text.empty()){tmp.push_back({ts,text}); timed=true;}
            } else { tmp.push_back({0.0,line}); }
        }
        if (timed) std::sort(tmp.begin(),tmp.end(),[](auto&a,auto&b){return a.time_sec<b.time_sec;});
        std::lock_guard<std::mutex> lk(mtx_);
        if (!tmp.empty()){lines_=std::move(tmp); has_ts_=timed; state_=State::FOUND; status_="Lyrics fetched ✓";}
        else {state_=State::NOT_FOUND; status_="No lyrics found";}
    }
};
