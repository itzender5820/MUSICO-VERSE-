/*  MUSICO VERSE 1.1  ─  CLI Music Player for Termux/Android  */
#include <ncurses.h>
#include <locale.h>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <thread>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <filesystem>
#include <sstream>

#include "player.h"
#include "lyrics.h"
#include "settings.h"

namespace fs = std::filesystem;
using clk = std::chrono::steady_clock;

// ─── Color pair IDs ──────────────────────────────────────────────────────────
enum CP {
    CP_TITLE=1, CP_BORDER, CP_META_KEY, CP_META_VAL,
    CP_PROGRESS, CP_PLAYLIST, CP_PL_ACTIVE,
    CP_LYR_DIM, CP_LYR_HI, CP_NOISE, CP_VIZ, CP_STATUS,
    CP_SET_HDR, CP_SET_ITEM, CP_SET_SEL,
    CP_COUNT
};

// ncurses color slots we own (16 slots, one per logical color)
// We start at slot 16 to avoid clobbering the standard 0-15
enum CSLOT {
    CS_VIZ=16, CS_BORDER, CS_TITLE, CS_META_KEY, CS_META_VAL,
    CS_PROGRESS, CS_PLAYLIST, CS_PL_FG, CS_PL_BG,
    CS_LYR_DIM, CS_LYR_HI, CS_NOISE, CS_STATUS,
    CS_SET_HDR_FG=230, CS_SET_HDR_BG=231,  // white on dark
    CS_SET_ITEM_FG=232, CS_SET_SEL_FG=233, CS_SET_SEL_BG=234
};

static Settings g_cfg;

// ─── Core: set one ncurses color slot from Color{ansi, brightness} ───────────
// Uses init_color() with exact RGB — bypasses terminal palette entirely.
static void set_ncurses_color(short slot, const Color& col) {
    short r,g,b;
    ansi256_to_rgb1000(col.ansi, r, g, b);
    apply_brightness(r, g, b, col.brightness);
    init_color(slot, r, g, b);
}

// Re-apply every color from g_cfg to ncurses
static void apply_colors() {
    auto& C = g_cfg.colors;
    set_ncurses_color(CS_VIZ,      C.viz);
    set_ncurses_color(CS_BORDER,   C.border);
    set_ncurses_color(CS_TITLE,    C.title);
    set_ncurses_color(CS_META_KEY, C.meta_key);
    set_ncurses_color(CS_META_VAL, C.meta_val);
    set_ncurses_color(CS_PROGRESS, C.progress);
    set_ncurses_color(CS_PLAYLIST, C.playlist);
    set_ncurses_color(CS_PL_FG,    C.pl_active_fg);
    set_ncurses_color(CS_PL_BG,    C.pl_active_bg);
    set_ncurses_color(CS_LYR_DIM,  C.lyr_dim);
    set_ncurses_color(CS_LYR_HI,   C.lyr_hi);
    set_ncurses_color(CS_NOISE,    C.noise);
    set_ncurses_color(CS_STATUS,   C.status);

    // Fixed UI colors for settings overlay
    init_color(CS_SET_HDR_FG,  1000,1000,1000);
    init_color(CS_SET_HDR_BG,  80, 80, 150);
    init_color(CS_SET_ITEM_FG, 800,800,800);
    init_color(CS_SET_SEL_FG,  0,  0,  0);
    init_color(CS_SET_SEL_BG,  200,700,1000);

    init_pair(CP_VIZ,      CS_VIZ,      -1);
    init_pair(CP_BORDER,   CS_BORDER,   -1);
    init_pair(CP_META_KEY, CS_META_KEY, -1);
    init_pair(CP_META_VAL, CS_META_VAL, -1);
    init_pair(CP_PROGRESS, CS_PROGRESS, -1);
    init_pair(CP_PLAYLIST, CS_PLAYLIST, -1);
    init_pair(CP_PL_ACTIVE,CS_PL_FG,   CS_PL_BG);
    init_pair(CP_LYR_DIM,  CS_LYR_DIM, -1);
    init_pair(CP_LYR_HI,   CS_LYR_HI,  -1);
    init_pair(CP_NOISE,    CS_NOISE,    -1);
    init_pair(CP_TITLE,    CS_TITLE,    -1);
    init_pair(CP_STATUS,   CS_STATUS,   -1);
    init_pair(CP_SET_HDR,  CS_SET_HDR_FG, CS_SET_HDR_BG);
    init_pair(CP_SET_ITEM, CS_SET_ITEM_FG,-1);
    init_pair(CP_SET_SEL,  CS_SET_SEL_FG, CS_SET_SEL_BG);
}

// ─── UTF-8 codepoint printer ─────────────────────────────────────────────────
static void waddcp(WINDOW* w, uint32_t cp) {
    char b[5]={};
    if      (cp<0x80)    { b[0]=(char)cp; }
    else if (cp<0x800)   { b[0]=(char)(0xC0|(cp>>6));   b[1]=(char)(0x80|(cp&0x3F)); }
    else if (cp<0x10000) { b[0]=(char)(0xE0|(cp>>12));  b[1]=(char)(0x80|((cp>>6)&0x3F));  b[2]=(char)(0x80|(cp&0x3F)); }
    else                 { b[0]=(char)(0xF0|(cp>>18));  b[1]=(char)(0x80|((cp>>12)&0x3F)); b[2]=(char)(0x80|((cp>>6)&0x3F)); b[3]=(char)(0x80|(cp&0x3F)); }
    waddstr(w,b);
}

static std::string fmt_time(double s) {
    if(s<0)s=0; char b[16]; snprintf(b,16,"%02d:%02d",(int)s/60,(int)s%60); return b;
}

// ─── Animated braille noise ───────────────────────────────────────────────────
struct NoiseGen {
    double phase=0.0;
    static constexpr uint32_t BRAILLE[9]={
        0x2800,0x28C0,0x28C4,0x28E4,0x28E6,0x28F4,0x28F6,0x28FE,0x28FF };
    static constexpr uint32_t SYMS[12]={
        0x00B7,0x2219,0x2022,0x25AA,0x25CF,0x25CB,
        0x2726,0x2727,0x2605,0x2606,0x00D7,0x007C };
    struct Cell { float base,speed,amp; };
    std::vector<Cell> cells; int W=0,H=0;
    void resize(int w,int h){
        if(w==W&&h==H)return; W=w;H=h; cells.resize(w*h);
        for(int i=0;i<w*h;++i){
            float s=(float)(i*2654435761u)/(float)UINT32_MAX;
            cells[i]={s,0.3f+0.7f*std::fmod(s*1.618f,1.f),0.5f+0.5f*std::fmod(s*2.718f,1.f)};
        }
    }
    void advance(double dt){ phase+=dt; }
    uint32_t get(int col,int row)const{
        if(col>=W||row>=H)return ' ';
        const Cell& c=cells[row*W+col];
        float v=0;
        v+=(float)std::sin(phase*c.speed*1.0+c.base*6.28f)*0.40f;
        v+=(float)std::sin(phase*c.speed*2.3+c.base*9.42f)*0.30f;
        v+=(float)std::sin(phase*c.speed*0.7+(float)col*0.3f)*0.20f;
        v+=(float)std::sin(phase*c.speed*3.1+(float)row*0.5f)*0.10f;
        v*=c.amp;
        float n=std::clamp((v+1.f)*0.5f,0.f,1.f);
        float sym_t=0.06f*(float)std::fabs(std::sin(phase*0.13+c.base*17.f));
        if(n<sym_t){ int si=((int)(c.base*97.f+phase*0.31f))%12; return SYMS[si<0?si+12:si]; }
        return BRAILLE[std::clamp((int)(n*8),0,8)];
    }
};
constexpr uint32_t NoiseGen::BRAILLE[9];
constexpr uint32_t NoiseGen::SYMS[12];

// ─── Settings overlay ────────────────────────────────────────────────────────
struct SettingsField {
    const char* label;
    Color*      color;   // null = viz_style field
    int*        intval;  // for viz_style
};

struct SettingsState {
    int  row=0;
    int  col=0;      // 0=ansi  1=brightness
    bool editing=false;
    std::string buf;
};

// Returns true = still open
static bool draw_settings(WINDOW* parent, SettingsState& st, int key) {
    int pww,pwh; getmaxyx(parent,pwh,pww);
    const int OW=68, OH=20;
    int oy=std::max(0,(pwh-OH)/2), ox=std::max(0,(pww-OW)/2);
    WINDOW* w=newwin(OH,OW,oy,ox);
    keypad(w,TRUE);

    static int viz_int=(int)g_cfg.viz_style;
    viz_int=(int)g_cfg.viz_style;

    // Field table
    SettingsField fields[]={
        {"Visualizer Style", nullptr,         &viz_int},
        {"Visualizer",       &g_cfg.colors.viz,          nullptr},
        {"Border / UI",      &g_cfg.colors.border,       nullptr},
        {"Title bar",        &g_cfg.colors.title,        nullptr},
        {"Meta key labels",  &g_cfg.colors.meta_key,     nullptr},
        {"Track name",       &g_cfg.colors.meta_val,     nullptr},
        {"Progress bar",     &g_cfg.colors.progress,     nullptr},
        {"Playlist text",    &g_cfg.colors.playlist,     nullptr},
        {"Active song (fg)", &g_cfg.colors.pl_active_fg, nullptr},
        {"Active song (bg)", &g_cfg.colors.pl_active_bg, nullptr},
        {"Lyrics (dim)",     &g_cfg.colors.lyr_dim,      nullptr},
        {"Lyrics (active)",  &g_cfg.colors.lyr_hi,       nullptr},
        {"Noise field",      &g_cfg.colors.noise,        nullptr},
        {"Status bar",       &g_cfg.colors.status,       nullptr},
    };
    const int NF=(int)(sizeof(fields)/sizeof(fields[0]));
    const int VISIBLE=OH-5; // rows available for fields

    // ── Process key ─────────────────────────────────────────────────────────
    bool done=false;
    if (st.editing) {
        if (key=='\n'||key==KEY_ENTER) {
            try {
                int v=std::stoi(st.buf);
                if (fields[st.row].color==nullptr) {
                    // viz style
                    *fields[st.row].intval=std::clamp(v,0,VIZ_STYLE_COUNT-1);
                    g_cfg.viz_style=(VizStyle)*fields[st.row].intval;
                } else {
                    if (st.col==0) fields[st.row].color->ansi=std::clamp(v,0,255);
                    else           fields[st.row].color->brightness=std::clamp(v,0,100);
                    apply_colors();
                }
                g_cfg.save();
            } catch(...){}
            st.editing=false; st.buf="";
        } else if (key==27) {
            st.editing=false; st.buf="";
        } else if (key==KEY_BACKSPACE||key==127||key==8) {
            if (!st.buf.empty()) st.buf.pop_back();
        } else if ((key>='0'&&key<='9')||(key=='-'&&st.buf.empty())) {
            if (st.buf.size()<3) st.buf+=(char)key;
        }
    } else {
        if (key=='q'||key=='Q'||key==27||key=='s'||key=='S') done=true;
        else if (key==KEY_DOWN||key=='j') { st.row=(st.row+1)%NF; st.col=0; }
        else if (key==KEY_UP  ||key=='k') { st.row=(st.row-1+NF)%NF; st.col=0; }
        else if (key==KEY_RIGHT||key=='\t') {
            // Tab between ansi/brightness columns (skip for viz style row)
            if (fields[st.row].color) st.col=1-st.col;
        }
        else if (key==KEY_LEFT)  { if (fields[st.row].color) st.col=1-st.col; }
        else if (key=='\n'||key==KEY_ENTER) {
            st.editing=true;
            if (fields[st.row].color==nullptr)
                st.buf=std::to_string(*fields[st.row].intval);
            else if (st.col==0)
                st.buf=std::to_string(fields[st.row].color->ansi);
            else
                st.buf=std::to_string(fields[st.row].color->brightness);
        }
    }

    // ── Draw ────────────────────────────────────────────────────────────────
    werase(w);
    wattron(w,COLOR_PAIR(CP_SET_HDR)|A_BOLD);
    box(w,0,0);
    const char* hdr=" ✦ SETTINGS ✦ ";
    mvwprintw(w,0,(OW-(int)strlen(hdr))/2,"%s",hdr);
    mvwprintw(w,1,2,"↑↓=navigate  Tab/←→=switch col  Enter=edit  S/Esc=close");
    // Column headers
    wattron(w,A_UNDERLINE);
    mvwprintw(w,2,2,"%-22s  %-6s  %-5s  %s","ELEMENT","ANSI","BRITE","PREVIEW");
    wattroff(w,A_UNDERLINE);
    wattroff(w,COLOR_PAIR(CP_SET_HDR)|A_BOLD);

    // Scroll offset
    int scroll=std::max(0,st.row-VISIBLE+2);

    for (int i=0;i<NF;++i) {
        int display_row=i-scroll;
        if (display_row<0||display_row>=VISIBLE) continue;
        int wr=3+display_row;

        bool active=(i==st.row);
        if (active) wattron(w,COLOR_PAIR(CP_SET_SEL)|A_BOLD);
        else        wattron(w,COLOR_PAIR(CP_SET_ITEM));

        // Label
        mvwprintw(w,wr,2,"%-22s  ",fields[i].label);

        if (fields[i].color==nullptr) {
            // viz style row — single int value
            std::string vs;
            if (active&&st.editing) vs=st.buf+"_";
            else vs=std::to_string(viz_int)+" "+VIZ_STYLE_NAMES[std::clamp(viz_int,0,VIZ_STYLE_COUNT-1)];
            wprintw(w,"%-30s",vs.c_str());
        } else {
            Color& col=*fields[i].color;
            // ANSI column
            bool ansi_sel=(active&&st.col==0);
            bool bri_sel =(active&&st.col==1);
            if (!active) wattroff(w,COLOR_PAIR(CP_SET_ITEM));

            // Ansi value
            if (ansi_sel) wattron(w,COLOR_PAIR(CP_SET_SEL)|A_BOLD|A_UNDERLINE);
            else if (active) wattron(w,COLOR_PAIR(CP_SET_SEL)|A_BOLD);
            else  wattron(w,COLOR_PAIR(CP_SET_ITEM));
            std::string av=(ansi_sel&&st.editing)?st.buf+"_":std::to_string(col.ansi);
            wprintw(w,"%-6s  ",av.c_str());
            if (ansi_sel) wattroff(w,A_UNDERLINE);

            // Brightness value
            if (bri_sel) wattron(w,COLOR_PAIR(CP_SET_SEL)|A_BOLD|A_UNDERLINE);
            else if (active) wattron(w,COLOR_PAIR(CP_SET_SEL)|A_BOLD);
            else  wattron(w,COLOR_PAIR(CP_SET_ITEM));
            std::string bv=(bri_sel&&st.editing)?st.buf+"_":std::to_string(col.brightness)+"%";
            wprintw(w,"%-5s  ",bv.c_str());
            if (bri_sel) wattroff(w,A_UNDERLINE);

            // Colour preview block — 5 filled braille chars in the actual color
            // We temporarily write colored text via escape (best we can in ncurses)
            wattron(w,COLOR_PAIR(CP_SET_ITEM));
            wprintw(w,"[");
            // Draw 5 preview chars — use the actual color pair if possible
            // We can't easily switch pairs mid-line, so just use ██████
            for(int p=0;p<5;++p) waddch(w,ACS_BLOCK);
            wprintw(w,"]");
        }

        if (active) wattroff(w,COLOR_PAIR(CP_SET_SEL)|A_BOLD);
        else        wattroff(w,COLOR_PAIR(CP_SET_ITEM));
    }

    // Footer: current selected color info
    if (st.row<NF && fields[st.row].color) {
        Color& col=*fields[st.row].color;
        wattron(w,COLOR_PAIR(CP_SET_HDR));
        mvwprintw(w,OH-2,2,
            "Format: <ANSI, BRIGHTNESS>  e.g. <51, 100> = bright cyan at full brightness");
        mvwprintw(w,OH-1,2,
            "Current: ANSI=%3d  Brightness=%3d%%  (0=black, 100=full color)",
            col.ansi, col.brightness);
        wattroff(w,COLOR_PAIR(CP_SET_HDR));
    }

    wrefresh(w);
    if (done) {
        // Erase the overlay region on stdscr before destroying the window
        // so no box/text artifacts are left behind
        werase(w);
        wrefresh(w);
    }
    delwin(w);
    return !done;
}

// ─── Title bar ───────────────────────────────────────────────────────────────
static void draw_titlebar(WINDOW* w, bool repeat, int vol,
                          const std::string& state_str) {
    int ww,wh; getmaxyx(w,wh,ww); (void)wh;
    werase(w);
    wattron(w,COLOR_PAIR(CP_TITLE)|A_BOLD);
    const char* title="✦ MUSICO VERSE ✦";
    mvwprintw(w,0,(ww-(int)strlen(title))/2,"%s",title);
    mvwprintw(w,0,1,"%s",state_str.c_str());
    char fl[64];
    snprintf(fl,64,"[R:%s V:%d%%] [S=Settings]",
             repeat?"ON":"off",vol);
    mvwprintw(w,0,ww-(int)strlen(fl)-1,"%s",fl);
    wattroff(w,COLOR_PAIR(CP_TITLE)|A_BOLD);
}

// ─── Meta + Lyrics/Noise ─────────────────────────────────────────────────────
static void draw_meta_lyrics(WINDOW* w, const TrackMeta& m,
                              Lyrics& lyr, double pos, NoiseGen& noise)
{
    int ww,wh; getmaxyx(w,wh,ww);
    wattron(w,COLOR_PAIR(CP_BORDER)); box(w,0,0); wattroff(w,COLOR_PAIR(CP_BORDER));
    int mid=ww/2;
    wattron(w,COLOR_PAIR(CP_BORDER));
    for(int r=1;r<wh-1;++r) mvwaddch(w,r,mid,ACS_VLINE);
    mvwaddch(w,0,mid,ACS_TTEE); mvwaddch(w,wh-1,mid,ACS_BTEE);
    wattroff(w,COLOR_PAIR(CP_BORDER));

    // Left: metadata
    int lw=mid-3;
    auto kv=[&](int row, const char* key, const std::string& val){
        if(row>=wh-1)return;
        wattron(w,COLOR_PAIR(CP_META_KEY)|A_BOLD);
        mvwprintw(w,row,2,"%-9s",key);
        wattroff(w,COLOR_PAIR(CP_META_KEY)|A_BOLD);
        mvwprintw(w,row,11,": ");
        wattron(w,COLOR_PAIR(CP_META_VAL));
        mvwprintw(w,row,13,"%s",val.substr(0,std::max(0,lw-12)).c_str());
        wattroff(w,COLOR_PAIR(CP_META_VAL));
    };
    kv(2, "NAME",     m.title.empty()  ?"Unknown":m.title);
    kv(4, "ARTIST",   m.artist.empty() ?"Unknown":m.artist);
    kv(6, "ALBUM",    m.album.empty()  ?"Unknown":m.album);
    kv(8, "FORMAT",   m.format.empty() ?"?":m.format);
    kv(10,"DURATION", std::to_string(m.duration_sec)+"s");
    kv(12,"YEAR",     m.year>0?std::to_string(m.year):"Unknown");

    // Right: lyrics or noise
    int rx=mid+2, rw=ww-mid-3, rh=wh-2;
    if(rw<2||rh<1)return;

    if (lyr.has_lyrics()) {
        auto lyr_lines = lyr.visible(pos, rh);
        int  active = lyr.active_in_visible(pos, rh);
        for(int i=0;i<(int)lyr_lines.size()&&i<rh;++i){
            bool hi=(i==active);
            if(hi) wattron(w,COLOR_PAIR(CP_LYR_HI)|A_BOLD);
            else   wattron(w,COLOR_PAIR(CP_LYR_DIM));
            std::string disp=lyr_lines[i];
            if((int)disp.size()>rw) disp=disp.substr(0,rw);
            int xoff=hi?std::max(0,(rw-(int)disp.size())/2):0;
            mvwprintw(w,1+i,rx,"%-*s",rw," ");
            mvwprintw(w,1+i,rx+xoff,"%s",disp.c_str());
            if(hi) wattroff(w,COLOR_PAIR(CP_LYR_HI)|A_BOLD);
            else   wattroff(w,COLOR_PAIR(CP_LYR_DIM));
        }
    } else {
        auto lst=lyr.state();
        if(lst==Lyrics::State::FETCHING||lst==Lyrics::State::NOT_FOUND){
            std::string msg=lyr.status();
            if(!msg.empty()){
                wattron(w,COLOR_PAIR(CP_STATUS));
                mvwprintw(w,rh/2+1,rx+std::max(0,(rw-(int)msg.size())/2),"%s",msg.c_str());
                wattroff(w,COLOR_PAIR(CP_STATUS));
            }
        }
        noise.resize(rw,rh);
        wattron(w,COLOR_PAIR(CP_NOISE));
        for(int r=0;r<rh;++r){
            wmove(w,r+1,rx);
            for(int c=0;c<rw;++c) waddcp(w,noise.get(c,r));
        }
        wattroff(w,COLOR_PAIR(CP_NOISE));
    }
}

// ─── Progress bar ─────────────────────────────────────────────────────────────
static void draw_progress(WINDOW* w, double pos, double dur) {
    int ww,wh; getmaxyx(w,wh,ww); (void)wh;
    wattron(w,COLOR_PAIR(CP_BORDER)); box(w,0,0); wattroff(w,COLOR_PAIR(CP_BORDER));
    const int info_w=22, bar_w=std::max(4,ww-4-info_w);
    double pct=(dur>0.5)?std::clamp(pos/dur,0.0,1.0):0.0;
    int filled=(int)(pct*bar_w);
    wmove(w,1,1); waddch(w,'[');
    wattron(w,COLOR_PAIR(CP_PROGRESS)|A_BOLD);
    for(int i=0;i<bar_w;++i) waddch(w,i<filled?'#':'-');
    wattroff(w,COLOR_PAIR(CP_PROGRESS)|A_BOLD);
    waddch(w,']');
    char info[32];
    snprintf(info,32," [%3.0f%%] [%s/%s]",pct*100,fmt_time(pos).c_str(),fmt_time(dur).c_str());
    mvwprintw(w,1,bar_w+2,"%s",info);
}

// ─── Visualizer ──────────────────────────────────────────────────────────────
static void draw_visualizer(WINDOW* w, Player& player) {
    int ww,wh; getmaxyx(w,wh,ww);
    wattron(w,COLOR_PAIR(CP_BORDER)); box(w,0,0); wattroff(w,COLOR_PAIR(CP_BORDER));
    int vw=ww-2,vh=wh-2; if(vw<=0||vh<=0)return;
    player.visualizer().init(vw,vh);
    auto frame=player.visualizer().render(g_cfg.viz_style);
    wattron(w,COLOR_PAIR(CP_VIZ)|A_BOLD);
    for(int r=0;r<(int)frame.rows.size()&&r<vh;++r){
        wmove(w,r+1,1);
        for(int c=0;c<(int)frame.rows[r].size()&&c<vw;++c)
            waddcp(w,frame.rows[r][c]);
    }
    wattroff(w,COLOR_PAIR(CP_VIZ)|A_BOLD);
}

// ─── Playlist ────────────────────────────────────────────────────────────────
static void draw_playlist(WINDOW* w, const Playlist& pl) {
    int ww,wh; getmaxyx(w,wh,ww);
    wattron(w,COLOR_PAIR(CP_BORDER)); box(w,0,0); wattroff(w,COLOR_PAIR(CP_BORDER));
    auto& E=pl.entries(); int cur=pl.current_idx(),rows=wh-2;
    int start=std::max(0,cur-rows/2);
    int end=std::min((int)E.size(),start+rows);
    if(end-start<rows) start=std::max(0,end-rows);
    for(int i=start;i<end;++i){
        int row=i-start+1; bool active=(i==cur);
        if(active) wattron(w,COLOR_PAIR(CP_PL_ACTIVE)|A_BOLD);
        else       wattron(w,COLOR_PAIR(CP_PLAYLIST));
        mvwprintw(w,row,1,"> %-*s<",ww-4,
            E[i].display_name.substr(0,(size_t)std::max(0,ww-5)).c_str());
        if(active) wattroff(w,COLOR_PAIR(CP_PL_ACTIVE)|A_BOLD);
        else       wattroff(w,COLOR_PAIR(CP_PLAYLIST));
    }
}


// ─── Search overlay ───────────────────────────────────────────────────────────
// Drawn over the playlist window. Returns false when user confirms/cancels.
struct SearchState {
    std::string query;
    std::vector<int> matches;   // indices into playlist
    int sel = 0;                // selected match index
};

static void update_search(SearchState& ss, const Playlist& pl) {
    ss.matches.clear();
    ss.sel = 0;
    if (ss.query.empty()) return;
    std::string ql = ss.query;
    std::transform(ql.begin(),ql.end(),ql.begin(),::tolower);
    auto& E = pl.entries();
    for (int i=0;i<(int)E.size();++i){
        std::string dl = E[i].display_name;
        std::transform(dl.begin(),dl.end(),dl.begin(),::tolower);
        if (dl.find(ql) != std::string::npos) ss.matches.push_back(i);
    }
}

// Draw search box + filtered results over given window.
// Returns: -1 = still open, -2 = cancelled, >=0 = playlist index chosen.
static int draw_search(WINDOW* w, SearchState& ss, const Playlist& pl, int key) {
    int ww,wh; getmaxyx(w,wh,ww);

    // Handle key
    int result = -1;
    if (key == 27) {                          // Esc = cancel
        result = -2;
    } else if (key == '\n' || key == KEY_ENTER) {
        if (!ss.matches.empty()) result = ss.matches[ss.sel];
        else result = -2;
    } else if (key == KEY_UP   || key == 'k') {
        if (ss.sel > 0) --ss.sel;
    } else if (key == KEY_DOWN || key == 'j') {
        if (ss.sel < (int)ss.matches.size()-1) ++ss.sel;
    } else if (key == KEY_BACKSPACE || key == 127 || key == 8) {
        if (!ss.query.empty()) { ss.query.pop_back(); update_search(ss,pl); }
    } else if (key >= 32 && key < 127) {
        ss.query += (char)key;
        update_search(ss,pl);
    }

    // Draw search box at top of playlist window
    werase(w);
    wattron(w,COLOR_PAIR(CP_BORDER)); box(w,0,0); wattroff(w,COLOR_PAIR(CP_BORDER));

    // Search bar row
    wattron(w,COLOR_PAIR(CP_LYR_HI)|A_BOLD);
    mvwprintw(w,1,2,"/ ");
    wattroff(w,COLOR_PAIR(CP_LYR_HI)|A_BOLD);
    wattron(w,COLOR_PAIR(CP_PLAYLIST));
    wprintw(w,"%-*s", ww-5, (ss.query+"_").substr(0,ww-5).c_str());
    wattroff(w,COLOR_PAIR(CP_PLAYLIST));

    // Divider
    wattron(w,COLOR_PAIR(CP_BORDER));
    mvwhline(w,2,1,ACS_HLINE,ww-2);
    wattroff(w,COLOR_PAIR(CP_BORDER));

    // Results
    int rows = wh-4;
    int start = std::max(0, ss.sel - rows/2);
    int end   = std::min((int)ss.matches.size(), start+rows);
    if (end-start<rows) start=std::max(0,end-rows);

    auto& E = pl.entries();
    if (ss.matches.empty() && !ss.query.empty()) {
        wattron(w,COLOR_PAIR(CP_STATUS));
        mvwprintw(w,3,2,"No results for: %s",ss.query.c_str());
        wattroff(w,COLOR_PAIR(CP_STATUS));
    }
    for (int i=start;i<end;++i){
        int row = 3+(i-start);
        bool active=(i==ss.sel);
        if(active) wattron(w,COLOR_PAIR(CP_PL_ACTIVE)|A_BOLD);
        else       wattron(w,COLOR_PAIR(CP_PLAYLIST));
        std::string name = E[ss.matches[i]].display_name;
        mvwprintw(w,row,1,"> %-*s<",ww-4,name.substr(0,std::max(0,ww-5)).c_str());
        if(active) wattroff(w,COLOR_PAIR(CP_PL_ACTIVE)|A_BOLD);
        else       wattroff(w,COLOR_PAIR(CP_PLAYLIST));
    }

    // Footer hint
    wattron(w,COLOR_PAIR(CP_STATUS));
    mvwprintw(w,wh-1,1,"↑↓=navigate  Enter=play  Esc=cancel  %d result(s)",
              (int)ss.matches.size());
    wattroff(w,COLOR_PAIR(CP_STATUS));

    wrefresh(w);
    return result;
}

// ─── Status bar ──────────────────────────────────────────────────────────────
static void draw_statusbar(WINDOW* w, const std::string& msg, const std::string& input) {
    int ww,wh; getmaxyx(w,wh,ww); (void)wh;
    werase(w);
    wattron(w,COLOR_PAIR(CP_STATUS));
    if(!input.empty()){
        mvwprintw(w,0,0,"Import path: %s_",input.substr(0,(size_t)std::max(0,ww-14)).c_str());
    } else {
        const char* hints="P=play/pause  R=repeat  1/2=vol  /=search  I=import  S=settings  Q=quit  ←→=seek  ↑↓=track";
        mvwprintw(w,0,0,"%-*s",ww,(!msg.empty()?msg:std::string(hints)).substr(0,(size_t)std::max(0,ww)).c_str());
    }
    wattroff(w,COLOR_PAIR(CP_STATUS));
}

// ═════════════════════════════════════════════════════════════════════════════
int main(int argc,char* argv[]) {
    (void)argc;(void)argv;
    setlocale(LC_ALL,"");
    g_cfg.load();

    initscr(); cbreak(); noecho();
    keypad(stdscr,TRUE); curs_set(0);
    nodelay(stdscr,TRUE); set_escdelay(50);
    start_color(); use_default_colors();

    if (!can_change_color()) {
        // Terminal can't set custom RGB — fallback message and continue with basic colors
        endwin();
        fprintf(stderr,
            "Warning: terminal does not support custom colors (TERM=%s).\n"
            "Try: export TERM=xterm-256color\n"
            "Continuing with basic color mode...\n",
            getenv("TERM")?getenv("TERM"):"?");
        // Re-init with basic mode
        initscr(); cbreak(); noecho();
        keypad(stdscr,TRUE); curs_set(0);
        nodelay(stdscr,TRUE); set_escdelay(50);
        start_color(); use_default_colors();
        // Fallback basic pairs
        init_pair(CP_VIZ,     COLOR_CYAN,   -1);
        init_pair(CP_BORDER,  COLOR_BLUE,   -1);
        init_pair(CP_META_KEY,COLOR_BLUE,   -1);
        init_pair(CP_META_VAL,COLOR_CYAN,   -1);
        init_pair(CP_PROGRESS,COLOR_GREEN,  -1);
        init_pair(CP_PLAYLIST,COLOR_WHITE,  -1);
        init_pair(CP_PL_ACTIVE,COLOR_BLACK, COLOR_CYAN);
        init_pair(CP_LYR_DIM, COLOR_WHITE,  -1);
        init_pair(CP_LYR_HI,  COLOR_YELLOW, -1);
        init_pair(CP_NOISE,   COLOR_GREEN,  -1);
        init_pair(CP_TITLE,   COLOR_WHITE,  -1);
        init_pair(CP_STATUS,  COLOR_WHITE,  -1);
        init_pair(CP_SET_HDR, COLOR_WHITE,  COLOR_BLUE);
        init_pair(CP_SET_ITEM,COLOR_WHITE,  -1);
        init_pair(CP_SET_SEL, COLOR_BLACK,  COLOR_CYAN);
    } else {
        apply_colors();
    }

    // Player
    Player player;
    if (!player.init()) {
        endwin();
        fprintf(stderr,"Player init failed: %s\n",player.status_msg.c_str());
        return 1;
    }
    player.visualizer().init(80,12);

    Lyrics   lyrics;
    NoiseGen noise;

    auto load_track=[&](const std::string& path){
        player.load_and_play(path);
        auto& m=player.current_meta();
        lyrics.load(path,m.title,m.artist);
        noise.phase=0.0;
    };
    if (player.playlist().count()>0){
        auto* e=player.playlist().current();
        if(e) load_track(e->path);
    }

    // Layout
    int SCOLS; { int dummy; getmaxyx(stdscr,dummy,SCOLS); (void)dummy; }
    int y=0;
    const int title_h=1,meta_h=16,prog_h=3,viz_h=12,status_h=1;
    int pl_rows=std::min(8,std::max(3,player.playlist().count()));
    int pl_h=pl_rows+2;

    WINDOW* wTitle  = newwin(title_h,SCOLS,y,0); y+=title_h;
    WINDOW* wMeta   = newwin(meta_h, SCOLS,y,0); y+=meta_h;
    WINDOW* wProg   = newwin(prog_h, SCOLS,y,0); y+=prog_h;
    WINDOW* wViz    = newwin(viz_h,  SCOLS,y,0); y+=viz_h;
    WINDOW* wPl     = newwin(pl_h,   SCOLS,y,0); y+=pl_h;
    WINDOW* wStatus = newwin(status_h,SCOLS,y,0);

    std::string status_msg, import_input;
    bool quit=false, settings_open=false, search_open=false;
    SettingsState ss;
    SearchState srch;
    auto last_frame=clk::now();

    while (!quit) {
        auto now=clk::now();
        double dt=std::chrono::duration<double>(now-last_frame).count();
        last_frame=now;
        noise.advance(dt);

        std::string state_str;
        switch(player.state()){
            case PlayerState::PLAYING: state_str="▶"; break;
            case PlayerState::PAUSED:  state_str="⏸"; break;
            case PlayerState::STOPPED: state_str="⏹"; break;
        }

        if (!settings_open) {
            draw_titlebar(wTitle,player.is_repeat(),player.volume(),state_str);
            wrefresh(wTitle);
            werase(wMeta);  draw_meta_lyrics(wMeta,player.current_meta(),lyrics,player.position(),noise); wrefresh(wMeta);
            werase(wProg);  draw_progress(wProg,player.position(),player.duration()); wrefresh(wProg);
            werase(wViz);   draw_visualizer(wViz,player); wrefresh(wViz);
            if (!search_open) { werase(wPl); draw_playlist(wPl,player.playlist()); wrefresh(wPl); }
            werase(wStatus);draw_statusbar(wStatus,status_msg,import_input); wrefresh(wStatus);
        }

        int ch=getch();

        if (settings_open) {
            settings_open=draw_settings(stdscr,ss,ch);
            if (!settings_open){
                apply_colors();
                // Nuke every cell on screen — no leftover overlay chars anywhere
                clear(); refresh();
                redrawwin(wTitle);  werase(wTitle);  wrefresh(wTitle);
                redrawwin(wMeta);   werase(wMeta);   wrefresh(wMeta);
                redrawwin(wProg);   werase(wProg);   wrefresh(wProg);
                redrawwin(wViz);    werase(wViz);    wrefresh(wViz);
                redrawwin(wPl);     werase(wPl);     wrefresh(wPl);
                redrawwin(wStatus); werase(wStatus); wrefresh(wStatus);
            }
            goto frame_end;
        }

        // ── Search mode ────────────────────────────────────────────────────────
        if (search_open) {
            int res = draw_search(wPl, srch, player.playlist(), ch);
            if (res == -2) {
                // Cancelled — restore playlist
                search_open = false;
                redrawwin(wPl); werase(wPl); wrefresh(wPl);
            } else if (res >= 0) {
                // Song chosen
                search_open = false;
                player.playlist().select(res);
                auto* e = player.playlist().current();
                if (e) { load_track(e->path); status_msg = ""; }
                redrawwin(wPl); werase(wPl); wrefresh(wPl);
            } else {
                // Still searching — redraw each key
                draw_search(wPl, srch, player.playlist(), ERR);
            }
            goto frame_end;
        }

        if (ch==ERR) goto frame_end;

        if (!import_input.empty()) {
            if      (ch=='\n'||ch==KEY_ENTER){ player.playlist().import(import_input); status_msg="Imported: "+import_input; import_input.clear(); }
            else if (ch==27)                 { import_input.clear(); }
            else if (ch==KEY_BACKSPACE||ch==127||ch==8){ if(!import_input.empty()) import_input.pop_back(); }
            else if (ch>=32&&ch<127)         { import_input+=(char)ch; }
        } else {
            switch(ch){
            case 'q':case 'Q': quit=true; break;
            case 'p':case 'P': player.play_pause(); break;
            case 'r':case 'R': player.toggle_repeat(); break;
            case '1': player.set_volume(+1); break;
            case '2': player.set_volume(-1); break;
            case 'k':case 'K': player.stop(); status_msg="Stopped"; break;
            case '/': search_open=true; srch=SearchState{}; draw_search(wPl,srch,player.playlist(),ERR); break;
            case 'i':case 'I': import_input=" "; status_msg=""; break;
            case 's':case 'S': settings_open=true; ss={0,0,false,""}; break;
            case KEY_DOWN:{
                player.playlist().select(player.playlist().current_idx()+1);
                auto* e=player.playlist().current(); if(e){load_track(e->path);status_msg="";}
                break;
            }
            case KEY_UP:{
                player.playlist().select(player.playlist().current_idx()-1);
                auto* e=player.playlist().current(); if(e){load_track(e->path);status_msg="";}
                break;
            }
            case KEY_RIGHT: player.seek(player.position()+10.0); break;
            case KEY_LEFT:  player.seek(std::max(0.0,player.position()-10.0)); break;
            default: break;
            }
        }

        frame_end:
        if (player.state()==PlayerState::PLAYING &&
            player.duration()>0.5 &&
            player.position()>=player.duration()-0.5)
        {
            if (player.is_repeat()) player.seek(0);
            else {
                player.next();
                auto* e=player.playlist().current();
                if(e){load_track(e->path);status_msg="";}
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    lyrics.cancel_fetch();
    player.shutdown();
    delwin(wTitle);delwin(wMeta);delwin(wProg);
    delwin(wViz);  delwin(wPl); delwin(wStatus);
    endwin();
    return 0;
}
