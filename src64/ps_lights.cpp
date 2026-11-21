#include "ps_lights.h"
#include <mutex>
#include <time.h>
#include <string.h>
#include <math.h>
#include <algorithm>

namespace {
constexpr int kPool = 128, kBeams = 16;
struct Slot { PSLight l; uint32_t seen; bool used; };
struct BSlot { PSBeam b; uint32_t seen; };
std::mutex g_mx;
Slot  g_pool[kPool];
BSlot g_beams[kBeams];
uint32_t g_frozenAt = 0; bool g_wasFrozen = false;

uint32_t NowMs() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)(t.tv_sec * 1000ull + t.tv_nsec / 1000000ull); }
float D2(const float* a, const float* b) { float x = a[0]-b[0], y = a[1]-b[1], z = a[2]-b[2]; return x*x + y*y + z*z; }
}

void Lights_Add(const float pos[3], float radius, float r, float g, float b, float intensity, int kind) {
    if (!(radius > 0.05f) || !(intensity > 0.f) || !isfinite(pos[0]) || !isfinite(pos[1]) || !isfinite(pos[2])) return;
    uint32_t now = NowMs();
    std::lock_guard<std::mutex> lk(g_mx);
    int freeI = -1, oldI = 0; uint32_t oldAge = 0;
    for (int i = 0; i < kPool; ++i) {
        Slot& s = g_pool[i];
        if (!s.used) { if (freeI < 0) freeI = i; continue; }
        if (D2(s.l.pos, pos) < 0.25f) {           // same light as last frame (0.5 m)
            s.l.radius = radius; s.l.col[0] = r; s.l.col[1] = g; s.l.col[2] = b; s.l.intensity = intensity; s.l.kind = kind;
            memcpy(s.l.pos, pos, 12); s.seen = now; return;
        }
        uint32_t age = now - s.seen; if (age > oldAge) { oldAge = age; oldI = i; }
    }
    Slot& s = g_pool[freeI >= 0 ? freeI : oldI];
    memcpy(s.l.pos, pos, 12); s.l.radius = radius; s.l.col[0] = r; s.l.col[1] = g; s.l.col[2] = b;
    s.l.intensity = intensity; s.l.kind = kind; s.seen = now; s.used = true;
}

void Lights_AddBeam(const float pos[3], const float dir[3], float intensity) {
    if (!isfinite(pos[0]) || !isfinite(dir[0])) return;
    uint32_t now = NowMs();
    std::lock_guard<std::mutex> lk(g_mx);
    int pick = -1, oldI = 0; uint32_t oldAge = 0;
    for (int i = 0; i < kBeams; ++i) {
        if (g_beams[i].seen && D2(g_beams[i].b.pos, pos) < 1.0f) { pick = i; break; }   // same lamp, moved < 1 m since last frame
        uint32_t age = g_beams[i].seen ? now - g_beams[i].seen : 0xffffffffu;
        if (age > oldAge) { oldAge = age; oldI = i; }
    }
    BSlot& s = g_beams[pick >= 0 ? pick : oldI];
    memcpy(s.b.pos, pos, 12); memcpy(s.b.dir, dir, 12); s.b.intensity = intensity; s.seen = now ? now : 1;
}

int Lights_Snapshot(const float cam[3], bool frozen, PSLight* out, int maxL, PSBeam* beams, int maxB, int* nBeams) {
    uint32_t now = NowMs();
    std::lock_guard<std::mutex> lk(g_mx);
    if (frozen && !g_wasFrozen) g_frozenAt = now;
    g_wasFrozen = frozen;
    uint32_t ref = frozen ? g_frozenAt : now;
    struct C { float d; int i; } c[kPool]; int n = 0;
    for (int i = 0; i < kPool; ++i) {
        Slot& s = g_pool[i];
        if (!s.used) continue;
        if ((int32_t)(ref - s.seen) > 180) { if (!frozen && now - s.seen > 2000) s.used = false; continue; }
        c[n].d = D2(s.l.pos, cam); c[n].i = i; ++n;
    }
    int k = std::min(n, maxL);
    std::partial_sort(c, c + k, c + n, [](const C& a, const C& b) { return a.d < b.d; });
    for (int j = 0; j < k; ++j) out[j] = g_pool[c[j].i].l;
    int nb = 0;
    if (beams && nBeams) {
        struct CB { float d; int i; } cb[kBeams]; int m = 0;
        for (int i = 0; i < kBeams; ++i) if ((int32_t)(ref - g_beams[i].seen) <= 120 && g_beams[i].seen) { cb[m].d = D2(g_beams[i].b.pos, cam); cb[m].i = i; ++m; }
        nb = std::min(m, maxB);
        std::partial_sort(cb, cb + nb, cb + m, [](const CB& a, const CB& b) { return a.d < b.d; });
        for (int j = 0; j < nb; ++j) beams[j] = g_beams[cb[j].i].b;
        *nBeams = nb;
    }
    return k;
}

int Lights_PoolCount() { std::lock_guard<std::mutex> lk(g_mx); int n = 0; for (auto& s : g_pool) n += s.used; return n; }
