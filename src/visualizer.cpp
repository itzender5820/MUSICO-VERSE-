#include "visualizer.h"
#include "kissfft/kiss_fft.h"
#include <cmath>
#include <algorithm>

// ─── init ────────────────────────────────────────────────────────────────────
void Visualizer::init(int w, int h) {
    std::lock_guard<std::mutex> lk(mtx_);
    int nw=std::max(1,w), nh=std::max(1,h);
    bool first=ring_.empty(), resize=(nw!=width_||nh!=height_);
    width_=nw; height_=nh;
    if (first||resize){ ring_.assign(FFT_SIZE,0.f); ring_write_=0; sample_rate_=44100; smooth_.fill(0.f); }
}

// ─── push_samples ────────────────────────────────────────────────────────────
void Visualizer::push_samples(const float* s, int frames, int ch, int sr) {
    std::lock_guard<std::mutex> lk(mtx_);
    sample_rate_=sr;
    for (int i=0;i<frames;++i){
        float m=0; for(int c=0;c<ch;++c) m+=s[i*ch+c]; m/=(float)ch;
        ring_[ring_write_]=m; ring_write_=(ring_write_+1)%FFT_SIZE;
    }
    compute_bands_locked();
}

// ─── FFT + band energy ────────────────────────────────────────────────────────
void Visualizer::compute_bands_locked() {
    std::vector<kiss_fft_cpx> in(FFT_SIZE),out(FFT_SIZE);
    for(int i=0;i<FFT_SIZE;++i){
        int idx=(ring_write_+i)%FFT_SIZE;
        float w=0.5f*(1.f-std::cos(2.f*(float)M_PI*i/(FFT_SIZE-1)));
        in[i]=kiss_fft_cpx(ring_[idx]*w,0.f);
    }
    auto cfg=kiss_fft_alloc(FFT_SIZE,0);
    kiss_fft(cfg,in.data(),out.data());
    kiss_fft_free(cfg);

    const float NORM=(float)FFT_SIZE*(float)FFT_SIZE;
    const float fr=(float)sample_rate_/FFT_SIZE;
    static const float edges[11]={30,80,200,400,600,850,1500,3000,5000,7000,12000};

    float right[10]={};
    for(int b=0;b<10;++b){
        int lo=std::max(1,(int)(edges[b]/fr));
        int hi=std::max(lo+1,(int)(edges[b+1]/fr));
        hi=std::min(hi,FFT_SIZE/2);
        float e=0; for(int k=lo;k<hi;++k) e+=std::norm(out[k]);
        e/=(float)(hi-lo);
        float db=10.f*std::log10(e/NORM+1e-12f);
        right[b]=std::clamp(db+60.f,0.f,60.f);
    }

    float full[TOTAL_BANDS];
    for(int i=0;i<9;++i) full[i]=right[9-i];
    full[9]=right[0];
    for(int i=0;i<9;++i) full[10+i]=right[i+1];

    static const float UP=0.25f, DOWN=0.15f;
    for(int b=0;b<TOTAL_BANDS;++b){
        float a=(full[b]>smooth_[b])?UP:DOWN;
        smooth_[b]=smooth_[b]*(1.f-a)+full[b]*a;
    }
}

// ─── Shared interpolation ────────────────────────────────────────────────────
std::vector<float> Visualizer::interpolate_cols(
    const std::array<float,TOTAL_BANDS>& snap, float scale) const
{
    std::vector<float> cols(width_,0.f);
    auto band_cx=[&](int b){ return (b+0.5f)/(float)TOTAL_BANDS*(float)width_; };
    for(int col=0;col<width_;++col){
        float cx=(float)col+0.5f;
        int bl=0;
        for(int b=0;b<TOTAL_BANDS;++b) if(band_cx(b)<=cx) bl=b;
        int br=std::min(bl+1,TOTAL_BANDS-1);
        float xl=band_cx(bl), xr=band_cx(br);
        float t=(xr>xl)?(cx-xl)/(xr-xl):0.f;
        t=std::clamp(t,0.f,1.f);
        float tc=(1.f-std::cos(t*(float)M_PI))*0.5f;
        float val=snap[bl]*(1.f-tc)+snap[br]*tc;
        cols[col]=(val/60.f)*(float)(height_*4)*scale;
    }
    return cols;
}

// ─── Style: BARS ─────────────────────────────────────────────────────────────
VisualizerFrame Visualizer::render_bars(const std::array<float,TOTAL_BANDS>& snap) {
    VisualizerFrame f; f.width=width_; f.height=height_;
    f.rows.assign(height_,std::vector<uint32_t>(width_,' '));
    auto cols=interpolate_cols(snap);
    for(int col=0;col<width_;++col){
        int tot=(int)std::round(cols[col]);
        for(int row=height_-1;row>=0&&tot>0;--row){
            int u=std::min(4,tot); tot-=u;
            f.rows[row][col]=(uint32_t)LEVEL_CHARS[u];
        }
    }
    return f;
}

// ─── Style: MIRROR (bars from centre up + down) ───────────────────────────────
VisualizerFrame Visualizer::render_mirror(const std::array<float,TOTAL_BANDS>& snap) {
    VisualizerFrame f; f.width=width_; f.height=height_;
    f.rows.assign(height_,std::vector<uint32_t>(width_,' '));
    int mid=height_/2;
    auto cols=interpolate_cols(snap, 0.5f); // half height up + down
    for(int col=0;col<width_;++col){
        int tot=(int)std::round(cols[col]);
        // upward from mid
        for(int row=mid;row>=0&&tot>0;--row){
            int u=std::min(4,tot); tot-=u;
            f.rows[row][col]=(uint32_t)LEVEL_CHARS[u];
        }
        // reset and go downward
        tot=(int)std::round(cols[col]);
        for(int row=mid+1;row<height_&&tot>0;++row){
            int u=std::min(4,tot); tot-=u;
            f.rows[row][col]=(uint32_t)LEVEL_CHARS[u];
        }
    }
    return f;
}

// ─── Style: WAVE (sine curve drawn in braille) ────────────────────────────────
VisualizerFrame Visualizer::render_wave(const std::array<float,TOTAL_BANDS>& snap) {
    VisualizerFrame f; f.width=width_; f.height=height_;
    f.rows.assign(height_,std::vector<uint32_t>(width_,' '));
    auto cols=interpolate_cols(snap);
    for(int col=0;col<width_;++col){
        // col_units is 0..(height*4) — convert to row + sub-char
        float units=cols[col];
        float row_f=(float)(height_*4)-units;  // from top: 0=top full, height*4=bottom empty
        if (row_f<0) row_f=0;
        int   row=(int)(row_f/4);
        int   sub=3-(int)(row_f)%4; // 0-3 sub-cell position
        if (row<height_){
            // Draw braille dot at (row, sub) — just the edge pixel
            f.rows[std::clamp(row,0,height_-1)][col]=(uint32_t)LEVEL_CHARS[std::max(1,sub)];
        }
    }
    return f;
}

// ─── Style: FIRE (block chars █▓▒░ from bottom) ───────────────────────────────
VisualizerFrame Visualizer::render_fire(const std::array<float,TOTAL_BANDS>& snap) {
    VisualizerFrame f; f.width=width_; f.height=height_;
    f.rows.assign(height_,std::vector<uint32_t>(width_,' '));
    auto cols=interpolate_cols(snap);
    for(int col=0;col<width_;++col){
        float frac=cols[col]/(float)(height_*4); // 0..1
        int full_rows=(int)(frac*height_);
        for(int row=height_-1;row>=(height_-full_rows)&&row>=0;--row){
            // Intensity: bottom = fullest, top = dimmest
            float intensity=(float)(height_-1-row)/(float)height_;
            intensity=1.f-intensity; // bottom bright
            int ci=std::clamp((int)(intensity*4),0,4);
            if (ci==0) ci=1;
            f.rows[row][col]=FIRE_CHARS[ci];
        }
    }
    return f;
}

// ─── Style: DOTS (single peak dot per column) ────────────────────────────────
VisualizerFrame Visualizer::render_dots(const std::array<float,TOTAL_BANDS>& snap) {
    VisualizerFrame f; f.width=width_; f.height=height_;
    f.rows.assign(height_,std::vector<uint32_t>(width_,' '));
    auto cols=interpolate_cols(snap);
    // Draw trailing bar (dimmer) + bright peak dot
    for(int col=0;col<width_;++col){
        float val=cols[col];
        if (val<1.f) continue;
        int peak_row=height_-1-(int)(val/4);
        peak_row=std::clamp(peak_row,0,height_-1);
        // Light trail underneath peak
        for(int row=height_-1;row>peak_row+1&&row>=0;--row)
            f.rows[row][col]=(uint32_t)LEVEL_CHARS[1]; // ⣀ faint
        // Peak line - full braille
        f.rows[peak_row][col]=(uint32_t)LEVEL_CHARS[4]; // ⣿
        // Dot above peak for extra flair
        if (peak_row>0)
            f.rows[peak_row-1][col]=0x2022; // •
    }
    return f;
}

// ─── Public render dispatch ───────────────────────────────────────────────────
VisualizerFrame Visualizer::render(VizStyle style) {
    std::array<float,TOTAL_BANDS> snap;
    { std::lock_guard<std::mutex> lk(mtx_); snap=smooth_; }

    switch(style){
        case VizStyle::MIRROR: return render_mirror(snap);
        case VizStyle::WAVE:   return render_wave(snap);
        case VizStyle::FIRE:   return render_fire(snap);
        case VizStyle::DOTS:   return render_dots(snap);
        default:               return render_bars(snap);
    }
}
