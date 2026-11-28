// 1.6.2 [SunShadows] the game's OWN shadows (peds, cars, objects, real-time ped shadows) follow the real sun.
//
// Idea taken from studying "Real Shadow Sun" (net.ytmodol.realshadowsun, closed source, no licence - nothing of it is copied here):
// GTA SA draws its classic projected shadows with three 2D vectors from the time cycle - ShadowFront (the length direction),
// ShadowSide (the width direction) and ShadowDisplacement (offset from the caster). On mobile they are reached through
// CTimeCycle::ValueProxy_m_fShadow*::m_Pointer and are indexed by CTimeCycle::m_CurrentStoredValue, like m_VectorToSun.
// Right after the game's CTimeCycle::Update we rotate them so the shadow points away from the sun in CTimeCycle::m_VectorToSun,
// keep the game's own lengths (scaled by Length / Width), and let low sun make long shadows. Our own code; GPU independent,
// so it works on every 32-bit and 64-bit device, also where the shadow-map receivers cannot be compiled.
#pragma once
#include <math.h>
#include <string.h>

namespace pssun {

struct State {
    float** proxy[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };   // front x/y, side x/y, displacement x/y
    float*  direct[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };   // fallback: plain arrays (other builds)
    bool    ok = false;
    int     applied = 0, skipped = 0, bad = 0;
    float   lastFront[2] = { 0, 0 };
    float   written[6] = { 0, 0, 0, 0, 0, 0 }; float base[3] = { 1, 1, 0 }; bool haveWritten = false;
};
inline State& S() { static State s; return s; }

template <class GetSym> inline bool Init(GetSym get)
{
    static const char* const kProxy[6] = {
        "_ZN10CTimeCycle26ValueProxy_m_fShadowFrontX9m_PointerE", "_ZN10CTimeCycle26ValueProxy_m_fShadowFrontY9m_PointerE",
        "_ZN10CTimeCycle25ValueProxy_m_fShadowSideX9m_PointerE",  "_ZN10CTimeCycle25ValueProxy_m_fShadowSideY9m_PointerE",
        "_ZN10CTimeCycle33ValueProxy_m_fShadowDisplacementX9m_PointerE", "_ZN10CTimeCycle33ValueProxy_m_fShadowDisplacementY9m_PointerE" };
    static const char* const kDirect[6] = {
        "_ZN10CTimeCycle14m_fShadowFrontXE", "_ZN10CTimeCycle14m_fShadowFrontYE", "_ZN10CTimeCycle13m_fShadowSideXE",
        "_ZN10CTimeCycle13m_fShadowSideYE", "_ZN10CTimeCycle21m_fShadowDisplacementXE", "_ZN10CTimeCycle21m_fShadowDisplacementYE" };
    State& s = S();
    int np = 0, nd = 0;
    for (int i = 0; i < 6; ++i) { s.proxy[i] = (float**)get(kProxy[i]); s.direct[i] = (float*)get(kDirect[i]); np += s.proxy[i] != nullptr; nd += s.direct[i] != nullptr; }
    s.ok = np == 6 || nd == 6;
    if (np != 6) for (int i = 0; i < 6; ++i) s.proxy[i] = nullptr;
    return s.ok;
}

inline float* Slot(int k, int idx)
{
    State& s = S();
    float* base = s.proxy[k] ? *s.proxy[k] : s.direct[k];
    return base ? base + idx : nullptr;
}

// vecToSun = CTimeCycle::m_VectorToSun (CVector[16]), idx = m_CurrentStoredValue. length / width: 1 = the game's own size.
inline void Apply(const float* vecToSun, const int* pIdx, float length, float width, bool lowSunLong)
{
    State& s = S();
    if (!s.ok || !vecToSun || !pIdx) { ++s.skipped; return; }
    const int idx = *pIdx;
    if (idx < 0 || idx >= 16) { ++s.skipped; return; }
    const float* sun = vecToSun + idx * 3;
    if (!isfinite(sun[0]) || !isfinite(sun[1]) || !isfinite(sun[2]) || sun[2] < 0.02f) { ++s.skipped; return; }   // night: the game's own (moon / lamps)
    float hx = sun[0], hy = sun[1]; const float hl = sqrtf(hx * hx + hy * hy);
    if (hl < 0.02f) { ++s.skipped; return; }                                        // sun straight overhead: no direction to follow
    hx /= hl; hy /= hl;
    float* p[6];
    for (int k = 0; k < 6; ++k) { p[k] = Slot(k, idx); if (!p[k]) { ++s.skipped; return; } }
    float v[6];
    for (int k = 0; k < 6; ++k) { v[k] = *p[k]; if (!isfinite(v[k]) || fabsf(v[k]) > 50.f) { if (++s.bad > 30) s.ok = false; return; } }
    // the game normally writes fresh values in every Update; if it did not (still our numbers), reuse the game's last own lengths,
    // otherwise the scaling would compound frame after frame
    float fl, sl, dl;
    if (s.haveWritten && memcmp(v, s.written, sizeof(v)) == 0) { fl = s.base[0]; sl = s.base[1]; dl = s.base[2]; }
    else {
        fl = sqrtf(v[0] * v[0] + v[1] * v[1]); sl = sqrtf(v[2] * v[2] + v[3] * v[3]); dl = sqrtf(v[4] * v[4] + v[5] * v[5]);
        if (fl < 0.05f) fl = 1.f; if (sl < 0.05f) sl = 1.f;
        s.base[0] = fl; s.base[1] = sl; s.base[2] = dl;
    }
    // low sun = long shadow (cot of the elevation, kept in a sane range); the game's own lengths stay the reference at ~35 degrees
    float k = 1.f;
    if (lowSunLong) { const float el = sun[2] < 0.12f ? 0.12f : (sun[2] > 1.f ? 1.f : sun[2]); k = 0.57f / el; k = k < 0.6f ? 0.6f : (k > 2.4f ? 2.4f : k); }
    const float F = fl * length * k, W = sl * width, Dd = dl * k;
    *p[0] = -hx * F;  *p[1] = -hy * F;           // shadow points away from the sun
    *p[2] = -hy * W;  *p[3] =  hx * W;           // perpendicular
    *p[4] = -hx * Dd; *p[5] = -hy * Dd;
    for (int j = 0; j < 6; ++j) s.written[j] = *p[j];
    s.haveWritten = true;
    s.lastFront[0] = *p[0]; s.lastFront[1] = *p[1];
    ++s.applied;
}

} // namespace pssun
