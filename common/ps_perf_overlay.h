// TyFMCGFX 1.6.1 performance overlay (FrameScope / FrameX-style readout, written for this mod - no code taken from those apps).
// FPS, 1% low, frame-time graph, CPU temperature and free RAM, drawn on ImGui's foreground list. Everything here is
// read without root: frame times come from ImGui's per-frame delta, the temperature from /sys/class/thermal and the
// memory from /proc/meminfo (both polled once a second; if Android denies a file the line is simply not shown).
#pragma once
#include "imgui.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

namespace psperf {

struct State
{
    float ms[240] = {};          // last 240 frame times (ms), ring buffer
    int   head = 0, count = 0;
    float pollT = 0.f;
    float tempC = -1.f;          // -1 = unknown
    float memFreeMb = -1.f, memTotalMb = -1.f;
    float shownFps = 0.f, shownLow = 0.f, shownAvgMs = 0.f, shownMaxMs = 0.f;
    float statT = 0.f;
    int   zone = -2;             // thermal zone index used, -2 = not searched yet, -1 = none readable
};

inline State& S() { static State s; return s; }

inline bool ReadFloat(const char* path, float& out)
{
    FILE* f = fopen(path, "r"); if (!f) return false;
    double v = 0; int ok = fscanf(f, "%lf", &v); fclose(f);
    if (ok != 1) return false; out = (float)v; return true;
}

// pick the first readable CPU-ish thermal zone (type contains cpu / soc / tsens / mtktscpu), else the first sane one
inline int FindZone()
{
    int fallback = -1;
    for (int i = 0; i < 48; ++i)
    {
        char p[96]; snprintf(p, sizeof(p), "/sys/class/thermal/thermal_zone%d/temp", i);
        float t; if (!ReadFloat(p, t)) continue;
        if (t > 1000.f) t /= 1000.f;
        if (t < 5.f || t > 130.f) continue;
        char tp[96], type[64] = {}; snprintf(tp, sizeof(tp), "/sys/class/thermal/thermal_zone%d/type", i);
        if (FILE* f = fopen(tp, "r")) { if (!fgets(type, sizeof(type), f)) type[0] = 0; fclose(f); }
        if (strstr(type, "cpu") || strstr(type, "soc") || strstr(type, "tsens") || strstr(type, "CPU")) return i;
        if (fallback < 0) fallback = i;
    }
    return fallback;
}

inline void Poll(State& st)
{
    if (st.zone == -2) st.zone = FindZone();
    if (st.zone >= 0)
    {
        char p[96]; snprintf(p, sizeof(p), "/sys/class/thermal/thermal_zone%d/temp", st.zone);
        float t; if (ReadFloat(p, t)) st.tempC = t > 1000.f ? t / 1000.f : t; else st.tempC = -1.f;
    }
    if (FILE* f = fopen("/proc/meminfo", "r"))
    {
        char line[128]; long tot = -1, avail = -1;
        while (fgets(line, sizeof(line), f))
        {
            if (!strncmp(line, "MemTotal:", 9)) tot = atol(line + 9);
            else if (!strncmp(line, "MemAvailable:", 13)) avail = atol(line + 13);
        }
        fclose(f);
        if (tot > 0) st.memTotalMb = tot / 1024.f;
        if (avail >= 0) st.memFreeMb = avail / 1024.f;
    }
}

// Call once per frame. dt = seconds since last frame. corner: 0 TL, 1 TR, 2 BL, 3 BR. fontPx = UI font size.
inline void Draw(float dt, int W, int H, int corner, float fontPx)
{
    State& st = S();
    if (dt > 0.f && dt < 1.f) { st.ms[st.head] = dt * 1000.f; st.head = (st.head + 1) % 240; if (st.count < 240) ++st.count; }
    st.pollT -= dt; if (st.pollT <= 0.f) { Poll(st); st.pollT = 1.f; }
    st.statT -= dt;
    if (st.statT <= 0.f && st.count > 0)
    {
        // average FPS + "1% low" = FPS of the slowest 1% of frames (min 1 frame) over the window
        float sum = 0.f, mx = 0.f; float sorted[240];
        for (int i = 0; i < st.count; ++i) { sorted[i] = st.ms[i]; sum += st.ms[i]; if (st.ms[i] > mx) mx = st.ms[i]; }
        int n = st.count / 100; if (n < 1) n = 1;
        for (int k = 0; k < n; ++k)              // partial selection: the n largest frame times
            for (int i = k + 1; i < st.count; ++i) if (sorted[i] > sorted[k]) { float t = sorted[k]; sorted[k] = sorted[i]; sorted[i] = t; }
        float slow = 0.f; for (int k = 0; k < n; ++k) slow += sorted[k]; slow /= n;
        st.shownAvgMs = sum / st.count; st.shownFps = 1000.f / fmaxf(st.shownAvgMs, 0.1f);
        st.shownLow = 1000.f / fmaxf(slow, 0.1f); st.shownMaxMs = mx; st.statT = 0.5f;
    }

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float fs = fontPx * 0.80f, pad = fs * 0.45f;
    const float gw = fs * 11.f, gh = fs * 2.6f;
    char l1[64], l2[64], l3[64] = "";
    snprintf(l1, sizeof(l1), "%.0f FPS   1%% low %.0f", st.shownFps, st.shownLow);
    snprintf(l2, sizeof(l2), "%.1f ms avg   %.1f ms max", st.shownAvgMs, st.shownMaxMs);
    int o = 0;
    if (st.tempC > 0.f) o += snprintf(l3 + o, sizeof(l3) - o, "CPU %.0f C   ", st.tempC);
    if (st.memFreeMb >= 0.f) snprintf(l3 + o, sizeof(l3) - o, "RAM free %.0f MB", st.memFreeMb);
    const int lines = l3[0] ? 3 : 2;
    const float bw = gw + pad * 2.f, bh = fs * 1.15f * lines + gh + pad * 3.f;
    const float m = fs * 0.6f;
    float x = (corner == 0 || corner == 2) ? m : W - bw - m;
    float y = (corner == 0 || corner == 1) ? m : H - bh - m;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bw, y + bh), IM_COL32(0, 0, 0, 150), fs * 0.35f);
    ImFont* font = ImGui::GetFont();
    const ImU32 fpsCol = st.shownFps >= 55.f ? IM_COL32(120, 230, 120, 255) : st.shownFps >= 28.f ? IM_COL32(240, 210, 90, 255) : IM_COL32(240, 100, 90, 255);
    float ty = y + pad;
    dl->AddText(font, fs, ImVec2(x + pad, ty), fpsCol, l1); ty += fs * 1.15f;
    dl->AddText(font, fs, ImVec2(x + pad, ty), IM_COL32(230, 230, 230, 255), l2); ty += fs * 1.15f;
    if (l3[0]) { dl->AddText(font, fs, ImVec2(x + pad, ty), IM_COL32(200, 200, 200, 255), l3); ty += fs * 1.15f; }
    // frame-time graph: 0..50 ms, 16.7 / 33.3 ms guide lines, newest on the right
    const float gx = x + pad, gy = ty + pad * 0.5f;
    dl->AddRectFilled(ImVec2(gx, gy), ImVec2(gx + gw, gy + gh), IM_COL32(255, 255, 255, 18));
    auto Y = [&](float msv) { float v = msv / 50.f; if (v > 1.f) v = 1.f; return gy + gh - v * gh; };
    dl->AddLine(ImVec2(gx, Y(16.7f)), ImVec2(gx + gw, Y(16.7f)), IM_COL32(120, 230, 120, 70));
    dl->AddLine(ImVec2(gx, Y(33.3f)), ImVec2(gx + gw, Y(33.3f)), IM_COL32(240, 210, 90, 70));
    if (st.count > 1)
    {
        const int n = st.count; ImVec2 prev;
        for (int i = 0; i < n; ++i)
        {
            const int idx = (st.head - n + i + 240) % 240;
            ImVec2 p(gx + gw * (float)i / 239.f + gw * (float)(240 - n) / 239.f, Y(st.ms[idx]));
            if (i) dl->AddLine(prev, p, st.ms[idx] > 33.4f ? IM_COL32(240, 100, 90, 255) : IM_COL32(150, 210, 255, 255), fmaxf(1.f, fs * 0.08f));
            prev = p;
        }
    }
}

} // namespace psperf
