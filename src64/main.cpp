// TyFMCGFX (v1.6.0, 64-bit source tree: C++/H + GLSL) - AML mod for GTA San Andreas Android 2.10 (arm64-v8a)
// Every shader is a .glsl file of the source tree (shaders/{vertex,fragment}, SAShaderL-style layout and uniforms) compiled INTO the .so, see ps_glsl.h.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <chrono>
#include <time.h>
#include <stdio.h>
#include "ps_settings.h"
#include "ps_glstate.h"
#include "ps_fx.h"
#include "ps_ui.h"
#include "ps_world.h"
#include "ps_shadow.h"
#include "ps_scene.h"
#include "ps_log.h"
#include "ps_lut.h"
#include "ps_glsl.h"
#include "ps_deferred.h"
#include "ps_lights.h"
#include "ps_api.h"
#include "sautils/isautils.h"
#include <dirent.h>
#include <strings.h>
#include <link.h>
#include <pthread.h>
#include <atomic>
#include <mutex>
#include <vector>
#include <GLES3/gl3.h>

#if defined(__LP64__)
  #define PS_BITS 64
  #define PS_GAME_INFO "GTA SA 2.10 (arm64-v8a)"
#else
  #define PS_BITS 32
  #define PS_GAME_INFO "GTA SA 2.00 (armeabi-v7a)"
#endif

MYMOD(net.propergfx.mobile, TyFMCGFX, 1.6.0, TyFMC)
NEEDGAME(com.rockstargames.gtasa)

// ---------------------------------------------------------------- game data (all optional, null-checked)
static float* g_pWetRoads = nullptr;
static float* g_pUnderWater = nullptr;
static float* g_pVectorToSun = nullptr;    // CVector[16]
static int*   g_pSunIndex = nullptr;       // CTimeCycle::m_CurrentStoredValue
static void*  g_pCamera = nullptr;         // &TheCamera
static float* g_pFov = nullptr;            // CDraw::ms_fFOV
static float* g_pAspect = nullptr;         // CDraw::ms_fAspectRatio
static unsigned char* g_pClockH = nullptr; // CClock::ms_nGameClockHours
static unsigned char* g_pClockM = nullptr; // CClock::ms_nGameClockMinutes
static unsigned char* g_pShdwTable = nullptr;  // CTimeCycle::m_nShadowStrength[8 hours][23 weathers] = the 'Shdw' column of timecyc.dat
static void UpdateTimecycStrength();
static void FlushPendingBufferWrites();
static unsigned* g_pTimeMs = nullptr;        // CTimer::m_snTimeInMilliseconds  -> Time
static float g_farClip = 800.f;              // last RwCameraSetFarClipPlane    -> FarClipDist
static volatile int g_entityModel = -1;      // CEntity::m_nModelIndex of the last CEntity::Render -> EntityModel
static void** g_pModelInfo = nullptr;        // CModelInfo::ms_modelInfoPtrs[20000]
static bool*  g_pUserPause = nullptr;        // CTimer::m_UserPause (the pause menu is open)
static bool*  g_pCodePause = nullptr;        // 1.5.0: CTimer::m_CodePause (menus / the game paused by code - libZyZGFX64 checks both)
static float* g_pRain = nullptr;             // 1.5.0: CWeather::Rain (falling rain amount)
static void* (*g_findPlayerVehicle)(int, bool) = nullptr;   // 1.5.0: FindPlayerVehicle - volumetric headlights
// 1.5.0 menu fix: the post effects run INSIDE the frame (render-queue event before CPostEffects::MobileRender / after RenderScene) on the
// game's scene target - the HUD, radar, main menu, pause menu and our floating button are drawn after that and never get tonemap / contrast.
static bool g_postHooked = false;            // CPostEffects::MobileRender found
static bool g_sceneThisFrame = false;        // the after-scene event ran since the last swap (render thread)
static bool g_fxThisFrame = false;           // FX ran inside the frame since the last swap
static int  g_eventFxMiss = 0;               // gameplay frames in a row without in-frame FX (-> swap-time fallback)
static int  g_sw = 0, g_sh = 0;              // EGL surface size
// 1.4.6 gameplay detection: the 3D world is rendered (RenderScene) only in gameplay - never in the main menu, the pause menu or a loading screen
static std::atomic<unsigned> g_sceneCalls{0};
static unsigned g_sceneSeen = 0; static int g_noSceneFrames = 1000;
static bool g_inGame = false, g_sceneHooked = false;
static pthread_t g_renderThread; static bool g_haveRenderThread = false;
// 1.6.0 (ZyZGfx 2.0 port): dynamic lights, beams, lightning, streaming memory, menu pause, frame capture
static unsigned char* g_pLightning = nullptr;   // CWeather::LightningFlash
static unsigned* g_pStreamMem = nullptr;         // CStreaming::ms_memoryAvailable (bytes)
static bool g_menuPaused = false;                // we set CTimer::m_CodePause while the menu is open
static float g_wetEff = 0.f, g_flash = 0.f;
static short* g_pWeatherOld = nullptr; static short* g_pWeatherNew = nullptr; static float* g_pWeatherInterp = nullptr;

static PSSettings g_cfg;
static char g_dir[512] = "";
static char g_iniPath[600] = "";
static bool g_initDone = false;
static EGLContext g_ctx = EGL_NO_CONTEXT;
static int  g_sunBad = 0;
static bool g_sunTracked = false;
static char g_guardPath[640] = "";
static bool g_guardActive = false, g_guardTripped = false;
static int  g_guardFrames = 0, g_guardTotal = 0;

static bool Readable(const void* p, size_t n)
{
    if (!p) return false;
    uintptr_t a = (uintptr_t)p & ~(uintptr_t)4095, e = ((uintptr_t)p + n + 4095) & ~(uintptr_t)4095;
    unsigned char vec[8];
    for (uintptr_t pg = a; pg < e; pg += 4096) if (mincore((void*)pg, 4096, vec) != 0) return false;
    return true;
}

// 1.6.0 Capture Frame: the default framebuffer (3D world + HUD, without our menu) -> files/TyFMCGFX/captures/capture_<time>.bmp
static void CaptureFrame(int w, int h)
{
    char dir[700]; snprintf(dir, sizeof(dir), "%s/captures", g_dir); mkdir(dir, 0777);
    std::vector<unsigned char> px((size_t)w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
    char path[800]; snprintf(path, sizeof(path), "%s/capture_%04d%02d%02d_%02d%02d%02d.bmp", dir, lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
    FILE* f = fopen(path, "wb");
    if (!f) { PS_FileLog("capture: can not write %s", path); return; }
    const int row = w * 3, pad = (4 - row % 4) % 4; const unsigned size = 54 + (unsigned)(row + pad) * h;
    unsigned char hd[54] = { 'B', 'M' };
    auto P32 = [&](int o, unsigned v) { hd[o] = v & 255; hd[o + 1] = (v >> 8) & 255; hd[o + 2] = (v >> 16) & 255; hd[o + 3] = v >> 24; };
    P32(2, size); P32(10, 54); P32(14, 40); P32(18, (unsigned)w); P32(22, (unsigned)h); hd[26] = 1; hd[28] = 24; P32(34, size - 54);
    fwrite(hd, 1, 54, f);
    std::vector<unsigned char> line(row + pad, 0);
    for (int y = 0; y < h; ++y)                 // GL rows are bottom-up, like BMP
    {
        const unsigned char* s = &px[(size_t)y * w * 4];
        for (int x = 0; x < w; ++x) { line[x * 3] = s[x * 4 + 2]; line[x * 3 + 1] = s[x * 4 + 1]; line[x * 3 + 2] = s[x * 4]; }
        fwrite(line.data(), 1, line.size(), f);
    }
    fclose(f);
    PS_FileLog("capture: %s (%dx%d)", path, w, h);
}

// Sun direction -> screen UV, using the camera matrix. Returns visibility 0..1.
static float ComputeSun(float& su, float& sv)
{
    g_sunTracked = false;
    if (g_sunBad > 30 || !g_pVectorToSun || !g_pSunIndex || !g_pCamera) return 0.f;
    int idx = *g_pSunIndex; if (idx < 0 || idx > 15) return 0.f;
    const float* sun = g_pVectorToSun + idx * 3;
    // CPlaceable: vtable, CSimpleTransform(pos[3], heading), CMatrix* m_matrix
    const size_t off = sizeof(void*) + 16;
    void* mp = *(void**)((char*)g_pCamera + off);
    if (!Readable(mp, 0x40)) { ++g_sunBad; return 0.f; }
    const float* m = (const float*)mp;       // right@0 top@4 at@8 (16-byte rows)
    const float* R = m; const float* U = m + 4; const float* F = m + 8;
    auto dot = [](const float* a, const float* b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; };
    auto len = [&](const float* a) { return sqrtf(dot(a, a)); };
    float lr = len(R), lu = len(U), lf = len(F);
    if (!(lr > 0.9f && lr < 1.1f && lu > 0.9f && lu < 1.1f && lf > 0.9f && lf < 1.1f) || fabsf(dot(R, F)) > 0.2f)
    { ++g_sunBad; return 0.f; }
    g_sunBad = 0;
    float x = dot(sun, R), y = dot(sun, U), z = dot(sun, F);
    float fov = (g_pFov && *g_pFov > 10.f && *g_pFov < 170.f) ? *g_pFov : 70.f;
    float asp = (g_pAspect && *g_pAspect > 0.5f && *g_pAspect < 4.f) ? *g_pAspect : 16.f / 9.f;
    g_sunTracked = true;
    if (z < 0.05f) return 0.f;
    float halfX = tanf(fov * 0.5f * 3.14159265f / 180.f), halfY = halfX / asp;
    su = (x / z) / halfX * 0.5f + 0.5f; sv = (y / z) / halfY * 0.5f + 0.5f;
    float horizon = sun[2] < -0.05f ? 0.f : (sun[2] > 0.15f ? 1.f : (sun[2] + 0.05f) / 0.2f);
    float edge = fmaxf(fabsf(su - 0.5f), fabsf(sv - 0.5f));
    float onscreen = edge < 0.7f ? 1.f : fmaxf(0.f, 1.f - (edge - 0.7f) / 0.8f);
    return horizon * onscreen;
}

// ---------------------------------------------------------------- per-frame work (render thread, before the buffer swap)
static void OnFrame()
{
    EGLContext ctx = eglGetCurrentContext();
    if (ctx == EGL_NO_CONTEXT) return;
    if (ctx != g_ctx)
    {
        if (g_ctx != EGL_NO_CONTEXT) { FX_ContextLost(); UI_ContextLost(); LUT_ContextLost(); World_ClearProgramCache(); Scene_ContextLost(); Shadow_ContextLost(); Deferred_ContextLost(); }
        g_ctx = ctx;
        PSGLState::InitCaps();
        g_initDone = false;
    }
    EGLDisplay dpy = eglGetCurrentDisplay(); EGLSurface surf = eglGetCurrentSurface(EGL_DRAW);
    EGLint w = 0, h = 0;
    if (!eglQuerySurface(dpy, surf, EGL_WIDTH, &w) || !eglQuerySurface(dpy, surf, EGL_HEIGHT, &h) || w < 16 || h < 16) return;

    static auto last = std::chrono::steady_clock::now();
    static double t0 = 0.0, accum = 0.0;
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last).count(); last = now;
    if (dt > 0.25f) dt = 0.25f; if (dt < 0.0005f) dt = 0.0005f;
    accum += dt; (void)t0;
    g_renderThread = pthread_self(); g_haveRenderThread = true;
    FlushPendingBufferWrites();
    {   // gameplay = the game rendered its 3D world in the last few frames and the pause menu is not open.
        // Post effects, the floating button / menu and the shadows only run in gameplay (1.4.6: they were also drawn over the main menu)
        const unsigned sc = g_sceneCalls.load(std::memory_order_relaxed);
        if (sc != g_sceneSeen) { g_sceneSeen = sc; g_noSceneFrames = 0; } else if (g_noSceneFrames < 1000) ++g_noSceneFrames;
        const bool paused = (g_pUserPause && *g_pUserPause) || (g_pCodePause && *g_pCodePause && !g_menuPaused);
        g_inGame = (!g_sceneHooked || g_noSceneFrames < 8) && !paused;
    }

    // time of day -> live Day/Night values
    float nf = (g_pClockH && g_pClockM && *g_pClockH < 24 && *g_pClockM < 60) ? PS_NightFactor(*g_pClockH, *g_pClockM) : 0.f;
    PS_UpdateLive(g_cfg, nf);
    float wet = g_pWetRoads ? *g_pWetRoads : 0.f;
    if (!isfinite(wet)) wet = 0.f;
    {   // 1.6.0 [Weather] Drying: the roads lose their wetness over DryingTime seconds instead of at once
        if (g_cfg.dryingOn && g_wetEff > wet) g_wetEff = fmaxf(wet, g_wetEff - dt / fmaxf(g_cfg.dryingTime, 1.f));
        else g_wetEff = wet;
        wet = g_wetEff;
    }
    {   // 1.6.0 [Menu] PauseGame: the game stands still while the menu is open
        const bool want = g_cfg.pauseInMenu && g_inGame && UI_MenuOpen() && g_pCodePause;
        if (want && !g_menuPaused) { if (!*g_pCodePause) { *g_pCodePause = true; g_menuPaused = true; } }
        else if (!want && g_menuPaused) { if (g_pCodePause) *g_pCodePause = false; g_menuPaused = false; }
    }
    if (g_pStreamMem && g_cfg.streamingMB > 0)
    {   // 1.6.0 [Performance] StreamingMemory (the game resets it on some loads: kept every frame)
        const unsigned want = (unsigned)g_cfg.streamingMB * 1024u * 1024u;
        if (*g_pStreamMem < want) *g_pStreamMem = want;
    }
    {   // lightning: the game's flag is on for single frames - hold + decay it
        const bool on = g_pLightning && *g_pLightning;
        g_flash = on ? 1.f : fmaxf(0.f, g_flash - dt * 4.f);
    }
    World_SetEnabled(g_cfg.enabled);
    g_cfg.shadowMethod = 0;                 // 1.4.6: always Deferred (like libZyZGFX64)
    g_sw = w; g_sh = h;
    // 1.5.0: the deferred lighting and the in-frame post effects need the render-queue events + the frame's camera even with shadows off
    Shadow_SetCameraService(g_cfg.enabled && (Deferred_Wanted(g_cfg) || g_cfg.postFx));
    Shadow_Configure(g_cfg, FX_Stats().adaptiveLevel);
    {   // deferred lighting inputs for the next frame (time, weather, sun, the player's headlights)
        PSDeferredFrame df;
        df.time = (float)fmod(accum, 3600.0); df.dt = dt; df.night = nf;
        df.wet = wet < 0.f ? 0.f : (wet > 1.f ? 1.f : wet);
        df.rain = g_pRain ? *g_pRain : 0.f; if (!isfinite(df.rain)) df.rain = 0.f;
        df.underwater = g_pUnderWater ? *g_pUnderWater : 0.f;
        if (g_pVectorToSun && g_pSunIndex) { int idx = *g_pSunIndex; if (idx >= 0 && idx < 16) { const float* p = g_pVectorToSun + idx * 3; if (isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2])) { df.sun[0] = p[0]; df.sun[1] = p[1]; df.sun[2] = p[2]; } } }
        if (g_cfg.headlightsOn && g_findPlayerVehicle && nf > 0.2f)
        {   // CVehicle: CPlaceable::m_matrix at +0x18 (right @0, forward @0x10, up @0x20, pos @0x30)
            const char* v = (const char*)g_findPlayerVehicle(-1, false);
            const char* m = (v && Readable(v + 0x18, 8)) ? *(const char* const*)(v + 0x18) : nullptr;
            if (m && Readable(m, 0x40))
            {
                const float* R = (const float*)m; const float* F = (const float*)(m + 0x10); const float* U = (const float*)(m + 0x20); const float* P = (const float*)(m + 0x30);
                if (isfinite(P[0]) && isfinite(F[0]) && fabsf(F[0] * F[0] + F[1] * F[1] + F[2] * F[2] - 1.f) < 0.1f)
                {
                    for (int i = 0; i < 3; ++i)
                    {
                        df.hl0[i] = P[i] + F[i] * 2.3f - R[i] * 0.65f + U[i] * 0.05f;
                        df.hl1[i] = P[i] + F[i] * 2.3f + R[i] * 0.65f + U[i] * 0.05f;
                        df.hlDir[i] = F[i] - U[i] * 0.08f;
                    }
                    df.headlights = true;
                }
            }
        }
        {   // 1.6.0 moon = opposite of the sun (the game moves both on the same arc), the lights of this frame
            float l = sqrtf(df.sun[0] * df.sun[0] + df.sun[1] * df.sun[1] + df.sun[2] * df.sun[2]);
            if (l > 0.1f) for (int i = 0; i < 3; ++i) df.moon[i] = -df.sun[i] / l;
            df.lightning = g_flash;
            float cam[3] = {0.f, 0.f, 0.f};
            const char* cm = g_pCamera ? *(const char* const*)((const char*)g_pCamera + 0x18) : nullptr;   // TheCamera.m_matrix
            if (cm && Readable(cm + 0x30, 12)) { const float* P = (const float*)(cm + 0x30); if (isfinite(P[0]) && isfinite(P[1]) && isfinite(P[2])) memcpy(cam, P, 12); }
            const bool frozen = (g_pUserPause && *g_pUserPause) || (g_pCodePause && *g_pCodePause);
            df.nLights = Lights_Snapshot(cam, frozen, df.lights, 32, df.beams, 8, &df.nBeams);
        }
        Deferred_SetFrame(df);
    }
    Scene_SetScreenSize(w, h);
    {   // the game's own sun vector (points TOWARD the sun, world space, z up) - the same vector the sun disc / coronas use
        float sv[3] = { 0.f, 0.f, 1.f }; bool ok = false;
        if (g_pVectorToSun && g_pSunIndex) { int idx = *g_pSunIndex; if (idx >= 0 && idx < 16) { const float* p = g_pVectorToSun + idx * 3; sv[0] = p[0]; sv[1] = p[1]; sv[2] = p[2]; ok = isfinite(sv[0]) && isfinite(sv[1]) && isfinite(sv[2]); } }
        float sl = 1.f;
        if (ok && sv[2] < -0.05f && g_cfg.moonShadows && g_cfg.moonLightOn)
        {   // 1.6.0 [Moon] Shadows: at night the dynamic shadows are cast by the moon (opposite of the sun), weaker
            sv[0] = -sv[0]; sv[1] = -sv[1]; sv[2] = -sv[2];
            sl = g_cfg.moonShadowStrength * fminf(1.f, nf * 1.5f);
        }
        Shadow_SetLightScale(sl);
        Shadow_SetSunHint(sv, ok);
        float uw0 = g_pUnderWater ? *g_pUnderWater : 0.f; uw0 = uw0 < 0.f ? 0.f : (uw0 > 1.f ? 1.f : uw0);
        Shadow_SetFrameGlobals(uw0, sv);
        UpdateTimecycStrength();
    }
    if (g_cfg.shadowRetry)
    {   // menu button "Retry shadows": clear the crash guard (takes effect after a game restart, the receivers are compiled into shaders at load)
        g_cfg.shadowRetry = false;
        if (g_guardPath[0]) remove(g_guardPath);
        g_guardTripped = false; g_guardActive = false; g_guardFrames = g_guardTotal = 0;
        Shadow_SetGuardFailed(false);
    }

    if (!g_initDone)
    {
        { char ld[700]; PS_LutDir(g_cfg, g_dir, ld, sizeof(ld)); LUT_Load(ld, g_cfg.lutFile); }
        g_initDone = true;
    }
    { char ld[700]; PS_LutDir(g_cfg, g_dir, ld, sizeof(ld)); LUT_Poll(ld, g_cfg.lutFile, dt); }     // 1.4: a LUT that is replaced / renamed / dropped in (or picked in the menu) is reloaded live


    PSGLState gs; gs.Save();
    PSFrameInfo fi; fi.w = w; fi.h = h; fi.time = (float)fmod(accum, 3600.0); fi.dt = dt;
    float uw = g_pUnderWater ? *g_pUnderWater : 0.f; fi.underwater = uw < 0.f ? 0.f : (uw > 1.f ? 1.f : uw);
    fi.sunVis = ComputeSun(fi.sunX, fi.sunY);
    {   // SAShaderL-compatible uniforms for every .glsl (and for game shaders that declare them)
        PSShaderGlobals g;
        if (g_pVectorToSun && g_pSunIndex) { int idx = *g_pSunIndex; if (idx >= 0 && idx < 16) { const float* p = g_pVectorToSun + idx * 3; if (isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2])) { g.sunVector[0] = p[0]; g.sunVector[1] = p[1]; g.sunVector[2] = p[2]; } } }
        g.timeMs = g_pTimeMs ? (int)*g_pTimeMs : (int)(accum * 1000.0);
        g.gameTimeSeconds = (g_pClockH && g_pClockM && *g_pClockH < 24 && *g_pClockM < 60) ? (int)*g_pClockH * 60 + (int)*g_pClockM : 720;
        g.underWaterness = fi.underwater;
        g.roadsWetness = wet < 0.f ? 0.f : (wet > 1.f ? 1.f : wet);
        g.farClipDist = g_farClip;
        g.entityModel = g_entityModel;
        g.timeSec = fi.time;
        memcpy(g.user, g_cfg.glslParam, sizeof(g.user));
        FX_SetGlobals(g);
    }
    {   // 1.5.0: normally FX already ran inside the frame (OnSceneEvent). Only when the render-queue events do not work on this game build
        // (or never reach the post point) the old swap-time path runs - gameplay only, as before.
        if (g_inGame && g_fxThisFrame) g_eventFxMiss = 0; else if (g_inGame && g_eventFxMiss < 1000) ++g_eventFxMiss;
        const bool fallback = !Scene_EventsReady() || g_eventFxMiss > 30;
        if (fallback && g_inGame && g_cfg.enabled && g_cfg.postFx && !FX_Failed()) { fi.targetFbo = 0; FX_Render(g_cfg, fi); if (Deferred_PostWanted(g_cfg)) Deferred_PostAA(g_cfg, 0, w, h); }
        g_fxThisFrame = false; g_sceneThisFrame = false;
        PSApi_Tick();
    }

    PSUIStatus st; st.deferred = &Deferred_Stats(); st.dir = g_dir; st.fxStatus = FX_Status(); st.gameInfo = PS_GAME_INFO; st.patchedShaders = World_PatchedCount();
    st.sunTracked = g_sunTracked; st.lutLoaded = FX_LutLoaded(); st.fx = &FX_Stats();
    st.scene = &Scene_Stats();
    st.shadow = &Shadow_Stats(); st.shadowTex = Shadow_Texture(); st.shadowReceivers = World_ShadowReceiverCount(); st.build = &World_BuildStats();
    if (g_cfg.captureNow) { g_cfg.captureNow = false; CaptureFrame(w, h); }   // 1.6.0 Capture Frame (before the menu is drawn)
    UI_Render(g_cfg, w, h, dt, st, g_inGame);   // hidden (and touch-transparent) outside gameplay
    Shadow_EndFrame();                  // ends this frame's capture (the cascades / mask already ran at the after-scene event)
    gs.Restore();
    World_BeginFrame(g_cfg, wet);       // after the replay: the new light matrix and the new map become active together
    World_OnShaderSelected();           // push the new uniforms into the shader the game keeps bound (it may not re-select it this frame)

    // crash guard: the guard file exists from launch until shadows have run for 120 captured frames (or the game has rendered ~1 minute).
    // v1.2 only counted frames with a successful replay (900 of them), so a quit before that - or a setup where nothing was replayed - left the file behind
    // and silently switched shadows off for every later launch.
    // 1.4.5 Deferred: nothing is compiled into the game at load, so the guard only covers the first seconds of the shadow pass itself:
    // armed when the first shadows are drawn, removed after 300 frames with shadows (~10 s). Closing the game normally later never trips it.
    static bool s_defGuardDone = false;
    if (g_cfg.shadowGuard && !g_guardActive && !s_defGuardDone && !g_guardTripped && g_guardPath[0] && Shadow_Stats().ready && Shadow_Stats().replayed > 0)
    { FILE* gf = fopen(g_guardPath, "w"); if (gf) { fputs("deferred shadow pass: first seconds\n", gf); fclose(gf); g_guardActive = true; } s_defGuardDone = true; }
    if (g_guardActive)
    {
        ++g_guardTotal;
        if (Shadow_Stats().replayed > 0) ++g_guardFrames;
        const bool done = g_guardFrames >= 300 || g_guardTotal >= 900;
        if (done) { remove(g_guardPath); g_guardActive = false; }
    }
}

// Shadow strength from timecyc.dat: CTimeCycle::m_nShadowStrength is the "Shdw" column of timecyc.dat, one value per (time slot, weather).
// The game blends those slots by the clock (0:00 5:00 6:00 7:00 12:00 19:00 20:00 22:00) and the two weathers by CWeather::InterpolationValue; this does the same
// and turns it into a gentle multiplier (relative to the strongest value in the file) so a modded timecyc.dat / storms / fog change how dark the shadows are.
// (The sun DIRECTION is not stored in timecyc.dat: the time-cycle code computes it from the clock - that is CTimeCycle::m_VectorToSun, used above.)
static void UpdateTimecycStrength()
{
    if (!g_pShdwTable || !g_pClockH || !g_pClockM || !g_pWeatherOld || !g_pWeatherNew || !g_pWeatherInterp) { Shadow_SetTimecyc(1.f, -1, 0, false); return; }
    static const int kSlot[9] = { 0, 300, 360, 420, 720, 1140, 1200, 1320, 1440 };
    int t = (int)*g_pClockH * 60 + (int)*g_pClockM; int wo = *g_pWeatherOld, wn = *g_pWeatherNew; float ip = *g_pWeatherInterp;
    if (t < 0 || t >= 1440 || wo < 0 || wo > 22 || wn < 0 || wn > 22 || !isfinite(ip)) { Shadow_SetTimecyc(1.f, -1, 0, false); return; }
    if (ip < 0.f) ip = 0.f; if (ip > 1.f) ip = 1.f;
    int i = 0; while (i < 7 && t >= kSlot[i + 1]) ++i;
    const float f = (float)(t - kSlot[i]) / (float)(kSlot[i + 1] - kSlot[i]); const int i2 = (i + 1) & 7;
    auto at = [&](int h, int w) { return (float)g_pShdwTable[h * 23 + w]; };
    const float vo = at(i, wo) + (at(i2, wo) - at(i, wo)) * f, vn = at(i, wn) + (at(i2, wn) - at(i, wn)) * f;
    const float cur = vo + (vn - vo) * ip;
    int mx = 0; for (int k = 0; k < 8 * 23; ++k) if (g_pShdwTable[k] > mx) mx = g_pShdwTable[k];
    if (mx <= 0) { Shadow_SetTimecyc(1.f, -1, 0, false); return; }
    float rel = cur / (float)mx; if (rel > 1.f) rel = 1.f; if (rel < 0.4f) rel = 0.4f;     // bounded: a wrong table layout can only soften shadows, never remove them
    Shadow_SetTimecyc(1.f + 0.6f * (rel - 1.f), (int)(cur + 0.5f), mx, true);
}

// ---------------------------------------------------------------- hooks
// NOTE: every hook forwards the original's return value. RQShader::BuildSource returns 1 (success) and the
// caller checks it; for functions that really return void the extra register value is simply ignored.
DECL_HOOKi(OS_ScreenSwapBuffers)
{
    OnFrame();
    return OS_ScreenSwapBuffers();
}

// The game compiles every shader in ES2Shader::Build (render thread, GL context current) and aborts the process when it fails. Patched pairs are test-built first.
DECL_HOOKi(ES2Shader_Build, void* self, const char* pxl, const char* vtx)
{
    std::string p, v;
    if (World_OnShaderBuild(pxl, vtx, p, v)) return ES2Shader_Build(self, p.c_str(), v.c_str());
    return ES2Shader_Build(self, pxl, vtx);
}

DECL_HOOKi(RQ_Command_rqSelectShader, char** ptr)
{
    int r = RQ_Command_rqSelectShader(ptr);
    World_OnShaderSelected();           // game has just called glUseProgram
    return r;
}

DECL_HOOKi(RQ_Command_rqDeleteShader, char** ptr)
{
    World_ClearProgramCache();          // GL program ids may be recycled
    return RQ_Command_rqDeleteShader(ptr);
}

DECL_HOOKi(RQShader_BuildSource, unsigned flags, const char** pxl, const char** vtx)
{
    int r = RQShader_BuildSource(flags, pxl, vtx);
    if (r) World_OnBuildSource(flags, pxl, vtx);   // swaps *pxl / *vtx for patched copies (the caller hashes + strdup()s them right after)
    return r;
}

// 1.5.0: TyFMCGFX's post effects on the scene target, inside the frame (render thread, GL state saved by the caller)
static void RunFrameFx()
{
    GLint fb = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
    GLuint dt = 0; int w = 0, h = 0;
    if (!Scene_DepthOf(fb, dt, w, h) || w <= 0 || h <= 0)
    {
        if (fb == 0) { w = g_sw; h = g_sh; }
        else { GLint vp[4] = {0, 0, 0, 0}; glGetIntegerv(GL_VIEWPORT, vp); w = vp[2]; h = vp[3]; }
    }
    if (w < 16 || h < 16) return;
    static auto last = std::chrono::steady_clock::now();
    static double accum = 0.0;
    auto now = std::chrono::steady_clock::now();
    float d = std::chrono::duration<float>(now - last).count(); last = now;
    if (d > 0.25f) d = 0.25f; if (d < 0.0005f) d = 0.0005f; accum += d;
    PSFrameInfo fi; fi.w = w; fi.h = h; fi.time = (float)fmod(accum, 3600.0); fi.dt = d;
    float uw = g_pUnderWater ? *g_pUnderWater : 0.f; fi.underwater = uw < 0.f ? 0.f : (uw > 1.f ? 1.f : uw);
    fi.sunVis = ComputeSun(fi.sunX, fi.sunY);
    fi.targetFbo = (unsigned)fb; fi.cocTex = Deferred_CocTexture();
    if (FX_Render(g_cfg, fi)) g_fxThisFrame = true;
    if (Deferred_PostWanted(g_cfg)) Deferred_PostAA(g_cfg, (GLuint)fb, w, h);
}

// ---------------------------------------------------------------- 1.4.3 deferred shadows (the libZyZyGFX64 way, see ps_scene.h)
// RenderScene() runs on the GAME thread and only records commands; Scene_PostEvent puts our own command before/after it into the render queue,
// so OnSceneEvent runs on the render thread exactly when the 3D world is finished and nothing 2D is drawn yet.
static void OnSceneEvent(int ev, int arg)
{
    if (ev == PS_EV_TAG) { Shadow_SetDrawKind(arg); return; }        // render thread: the kind of the entity whose draws follow
    if (g_ctx == EGL_NO_CONTEXT || eglGetCurrentContext() != g_ctx) return;
    if (ev == PS_EV_BEFORE_SCENE) { Scene_OnTargetSelected(); Shadow_BeginScene(); return; }
    if (ev == PS_EV_AFTER_SCENE)
    {
        Shadow_SetDrawKind(PS_KIND_NONE);
        if (g_sceneThisFrame) return;                                   // the world is rendered once per frame; extra RenderScene calls (mirrors) are ignored
        g_sceneThisFrame = true;
        const bool def = g_cfg.enabled && Deferred_Wanted(g_cfg);
        const bool fxHere = !g_postHooked && g_cfg.enabled && g_cfg.postFx && !FX_Failed();
        if (!Shadow_AfterSceneNeeded() && !def && !fxHere) return;
        PSGLState gs; gs.Save();
        if (Shadow_AfterSceneNeeded()) Shadow_AfterScene();
        if (def) Deferred_AfterScene(g_cfg, FX_Stats().adaptiveLevel);
        if (fxHere) RunFrameFx();                                      // no CPostEffects::MobileRender symbol: right after the world
        gs.Restore();
        World_OnShaderSelected();
        return;
    }
    if (ev == PS_EV_POST)
    {   // the 3D world + its effects are done, nothing 2D is drawn yet
        if (!g_sceneThisFrame || g_fxThisFrame) return;                // menus / loading screens: no world was rendered this frame
        if (!(g_cfg.enabled && g_cfg.postFx && !FX_Failed())) return;
        PSGLState gs; gs.Save();
        RunFrameFx();
        gs.Restore();
        World_OnShaderSelected();
    }
}

DECL_HOOKv(CPostEffects_MobileRender)
{
    Scene_PostEvent(PS_EV_POST);
    CPostEffects_MobileRender();
}

DECL_HOOKv(RenderScene, bool b)
{
    g_sceneCalls.fetch_add(1, std::memory_order_relaxed);   // gameplay detection (see OnFrame)
    Scene_PostEvent(PS_EV_BEFORE_SCENE);
    RenderScene(b);
    Scene_PostEvent(PS_EV_AFTER_SCENE);
}

// the game just bound a render target: a big one gets our readable depth texture (same size, same bits) instead of its depth renderbuffer
DECL_HOOKi(RQ_Command_rqTargetSelect, char** ptr)
{
    int r = RQ_Command_rqTargetSelect(ptr);
    if (Scene_Wanted()) Scene_OnTargetSelected();
    return r;
}

// Shadow capture: the stream layout {int mode, int count, uintptr_t offset} was read from the disassembly of rqDrawIndexed (2.00 and 2.10).
// Recording happens AFTER the original ran, so the GL state (program, buffers, attribute pointers, uniforms) is exactly what the draw used.
DECL_HOOKi(RQ_Command_rqDrawIndexed, char** ptr)
{
    if (!Shadow_WantCapture())
    {
        if (!Shadow_WantCamera()) return RQ_Command_rqDrawIndexed(ptr);
        int r = RQ_Command_rqDrawIndexed(ptr);
        Shadow_RecordCamera();          // deferred: this frame's camera (the mask is rebuilt every frame)
        return r;
    }
    const char* p = *ptr;
    const int mode = *(const int*)p, count = *(const int*)(p + 4);
    const uintptr_t off = *(const uintptr_t*)(p + 8);
    int r = RQ_Command_rqDrawIndexed(ptr);
    Shadow_RecordDraw(mode, count, off);
    return r;
}

DECL_HOOKv(AND_TouchEvent, int type, int finger, int x, int y)
{
    if (UI_OnTouch(type, finger, x, y)) return;
    AND_TouchEvent(type, finger, x, y);
}

DECL_HOOKp(RwCameraSetFarClipPlane, void* cam, float farPlane)
{
    // 1.6.0 [Performance] DrawDistance (ZyZ TuFarClip): only the main camera's far clip (50 .. 5000 m), never reflections / UI cameras
    if (farPlane > 50.f && farPlane < 5000.f && g_cfg.enabled && fabsf(g_cfg.drawDistance - 1.f) > 0.01f) farPlane *= g_cfg.drawDistance;
    if (farPlane > 50.f && farPlane < 100000.f) g_farClip = farPlane;   // FarClipDist (the main camera; tiny clips are reflections / UI)
    return RwCameraSetFarClipPlane(cam, farPlane);
}


// ---------------------------------------------------------------- 1.6.0 light sources (game thread) - port of ZyZGfx 2.0 dynlights / beams
struct PSVec3 { float x, y, z; };
static void AddPool(const PSVec3* pos, float fx, float fy, float sx, float sy, unsigned char r, unsigned char g, unsigned char b, int kind, float lift)
{
    if (!pos) return;
    const float R = fmaxf(sqrtf(fx * fx + fy * fy), sqrtf(sx * sx + sy * sy)) * 1.6f;
    if (!(R > 0.3f) || R > 60.f) return;
    float mx = (float)(r > g ? (r > b ? r : b) : (g > b ? g : b));
    if (mx < 4.f) return;
    float p[3] = { pos->x, pos->y, pos->z + fmaxf(lift, R * 0.4f) };
    Lights_Add(p, R, r / mx, g / mx, b / mx, fminf(fmaxf(mx / 128.f, 0.2f), 1.5f), kind);
}
DECL_HOOKb(StoreStaticShadow, unsigned long long id, unsigned char type, void* tex, PSVec3* pos, float fx, float fy, float sx, float sy, short intensity,
           unsigned char r, unsigned char g, unsigned char b, float zDist, float scale, float drawDist, bool temp, float upDist)
{
    if (type == 2 && g_cfg.enabled && g_cfg.deferredOn && g_cfg.dynLightsOn)   // 2 = additive light pool (street lamps, neon, building lights)
    {
        AddPool(pos, fx, fy, sx, sy, r, g, b, 0, 1.5f);
        if (g_cfg.replaceGameShadows) return false;
    }
    return StoreStaticShadow(id, type, tex, pos, fx, fy, sx, sy, intensity, r, g, b, zDist, scale, drawDist, temp, upDist);
}
DECL_HOOKv(StoreCarLightShadow, void* veh, int id, void* tex, PSVec3* pos, float fx, float fy, float sx, float sy, unsigned char r, unsigned char g, unsigned char b, float maxAngle)
{
    if (g_cfg.enabled && g_cfg.deferredOn && g_cfg.dynLightsOn) AddPool(pos, fx, fy, sx, sy, r, g, b, 1, 0.8f);
    StoreCarLightShadow(veh, id, tex, pos, fx, fy, sx, sy, r, g, b, maxAngle);
}
DECL_HOOKv(CPointLights_AddLight, unsigned char type, PSVec3 pos, PSVec3 dir, float radius, float r, float g, float b, unsigned char fog, bool extraShadows, void* ent)
{
    if (type <= 1 && g_cfg.enabled && g_cfg.deferredOn && g_cfg.dynLightsOn && radius > 0.3f && radius < 60.f)   // 0 = point light, 1 = point light, no fog
    {
        float mx = fmaxf(r, fmaxf(g, b));
        if (mx > 0.02f) { float p[3] = { pos.x, pos.y, pos.z }; Lights_Add(p, radius, r / mx, g / mx, b / mx, fminf(mx * 1.2f, 1.5f), 2); }
    }
    CPointLights_AddLight(type, pos, dir, radius, r, g, b, fog, extraShadows, ent);
}
DECL_HOOKv(DoHeadLightBeam, void* veh, int type, float* mat, unsigned char right)
{   // CMatrix: right @0, forward @0x10, up @0x20, pos @0x30
    if (mat && g_cfg.enabled && g_cfg.deferredOn && g_cfg.beamsOn)
    {
        const float* R = mat; const float* F = mat + 4; const float* U = mat + 8; const float* P = mat + 12;
        if (isfinite(P[0]) && isfinite(F[0]) && fabsf(F[0] * F[0] + F[1] * F[1] + F[2] * F[2] - 1.f) < 0.2f)
        {
            const float side = right ? 0.7f : -0.7f;
            float p[3], d[3];
            for (int i = 0; i < 3; ++i) { p[i] = P[i] + F[i] * 2.2f + R[i] * side + U[i] * 0.05f; d[i] = F[i] - U[i] * 0.08f; }
            Lights_AddBeam(p, d, 1.f);
            if (g_cfg.replaceGameBeams) return;
        }
    }
    DoHeadLightBeam(veh, type, mat, right);
}

// EntityModel: same as SAShaderL - remember the model of the entity being rendered (CEntity::m_nModelIndex, int16 at 0x32 on arm64)
// 1.4.6 shadow tags (the libZyZGFX64 way, GTA SA 2.10 arm64 layout): CEntity +0x20 m_pRwObject, +0x32 m_nModelIndex, +0x58 m_nNumLodChildren
// (non-zero = a LOD stand-in), +0x5a type in bits 0..2 (1 building, 2 vehicle, 3 ped, 4 object); CBaseModelInfo +0x2c flags & 0x7800 = 0x800 / 0x1000: tree / palm.
// The kind is put into the render queue before the entity's draws and cleared after them, so the shadow capture on the render thread knows
// what it records: static buildings / trees may be remembered, LOD stand-ins never cast near the camera, everything else casts only while drawn.
static int g_entityDepth = 0;                   // game thread: only the outermost CEntity::Render posts (CObject::Render -> CEntity::Render ...)
static int EntityKind(const char* e)
{
    if (!*(void* const*)(e + 0x20)) return PS_KIND_NONE;
    const unsigned type = (unsigned)(*(const unsigned char*)(e + 0x5a)) & 7u;
    const bool lod = *(const unsigned char*)(e + 0x58) != 0;
    if (type == 1)
    {
        const unsigned short mi = *(const unsigned short*)(e + 0x32);
        bool tree = false;
        if (g_pModelInfo && mi < 20000) { const char* info = (const char*)g_pModelInfo[mi]; if (info) { const unsigned f = *(const unsigned short*)(info + 0x2c) & 0x7800u; tree = f == 0x800u || f == 0x1000u; } }
        if (lod) return tree ? PS_KIND_LODTREE : PS_KIND_LOD;
        return tree ? PS_KIND_TREE : PS_KIND_BUILDING;
    }
    return PS_KIND_DYNAMIC;                     // vehicles, peds, objects (doors, barriers, breakables ... can move or vanish)
}
DECL_HOOKv(CEntity_Render, void* self)
{
    g_entityModel = self ? (int)*(short*)((char*)self + 0x32) : -1;
    const bool tag = self && g_entityDepth == 0 && Scene_Wanted() && Scene_EventsReady() && Scene_PostEvent(PS_EV_TAG, EntityKind((const char*)self));
    ++g_entityDepth;
    CEntity_Render(self);
    --g_entityDepth;
    if (tag) Scene_PostEvent(PS_EV_TAG, PS_KIND_NONE);
    g_entityModel = -1;         // same as SAShaderL
}

// ---------------------------------------------------------------- 1.4.6 GOT hooks on libGTASA's GL buffer imports
// A remembered shadow caster is only valid while its buffers hold the same mesh. The game streams models in / out, re-uses buffer
// names and refills dynamic buffers; every write by the GAME bumps the buffer's generation (ps_shadow.cpp), a cached caster whose
// generation changed is dropped instead of drawing whatever the buffer holds now (= the "ghost shadows"). Only libGTASA's import
// slots are patched, so our own GL calls and other mods are not affected.
static void (*o_glBufferData)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
static void (*o_glBufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*) = nullptr;
static void (*o_glDeleteBuffers)(GLsizei, const GLuint*) = nullptr;
static void* (*o_glMapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield) = nullptr;
static std::mutex g_bufMx; static std::vector<GLuint> g_bufPending;      // writes from another thread (shared context): applied on the render thread
static bool OnRenderThread() { return !g_haveRenderThread || pthread_equal(pthread_self(), g_renderThread); }
static void BufWrite(GLenum target)
{
    if (OnRenderThread()) { Shadow_OnBufferWrite(target); return; }
    GLint id = 0; glGetIntegerv(target == GL_ARRAY_BUFFER ? GL_ARRAY_BUFFER_BINDING : GL_ELEMENT_ARRAY_BUFFER_BINDING, &id);
    if (id > 0) { std::lock_guard<std::mutex> l(g_bufMx); if (g_bufPending.size() < 65536) g_bufPending.push_back((GLuint)id); }
}
static void h_glBufferData(GLenum t, GLsizeiptr n, const void* d, GLenum u) { o_glBufferData(t, n, d, u); if (t == GL_ARRAY_BUFFER || t == GL_ELEMENT_ARRAY_BUFFER) BufWrite(t); }
static void h_glBufferSubData(GLenum t, GLintptr o, GLsizeiptr n, const void* d) { o_glBufferSubData(t, o, n, d); if (t == GL_ARRAY_BUFFER || t == GL_ELEMENT_ARRAY_BUFFER) BufWrite(t); }
static void* h_glMapBufferRange(GLenum t, GLintptr o, GLsizeiptr n, GLbitfield a)
{
    void* r = o_glMapBufferRange(t, o, n, a);
    if ((a & GL_MAP_WRITE_BIT) && (t == GL_ARRAY_BUFFER || t == GL_ELEMENT_ARRAY_BUFFER)) BufWrite(t);
    return r;
}
static void h_glDeleteBuffers(GLsizei n, const GLuint* ids)
{
    if (OnRenderThread()) Shadow_OnBufferDelete(n, ids);
    else if (ids && n > 0) { std::lock_guard<std::mutex> l(g_bufMx); for (GLsizei i = 0; i < n && g_bufPending.size() < 65536; ++i) g_bufPending.push_back(ids[i]); }
    o_glDeleteBuffers(n, ids);
}
static void FlushPendingBufferWrites()
{
    std::vector<GLuint> v;
    { std::lock_guard<std::mutex> l(g_bufMx); if (g_bufPending.empty()) return; v.swap(g_bufPending); }
    Shadow_OnBufferDelete((GLsizei)v.size(), v.data());
}

// libGTASA's import slot (GOT entry) of `sym`: walks its dynamic relocations (JMPREL + RELA/REL)
struct GotQuery { const char* lib; const char* sym; void** slot; };
static int GotCb(struct dl_phdr_info* info, size_t, void* data)
{
    GotQuery* q = (GotQuery*)data;
    if (!info->dlpi_name || !strstr(info->dlpi_name, q->lib)) return 0;
    const ElfW(Addr) base = info->dlpi_addr;
    const ElfW(Dyn)* dyn = nullptr;
    for (int i = 0; i < info->dlpi_phnum; ++i) if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) dyn = (const ElfW(Dyn)*)(base + info->dlpi_phdr[i].p_vaddr);
    if (!dyn) return 0;
    ElfW(Addr) symtab = 0, strtab = 0, jmprel = 0, rel = 0; size_t jmpsz = 0, relsz = 0; ElfW(Sxword) pltrel = 0; bool rela = false;
    for (const ElfW(Dyn)* d = dyn; d->d_tag != DT_NULL; ++d)
    {
        switch (d->d_tag)
        {
        case DT_SYMTAB: symtab = d->d_un.d_ptr; break;
        case DT_STRTAB: strtab = d->d_un.d_ptr; break;
        case DT_JMPREL: jmprel = d->d_un.d_ptr; break;
        case DT_PLTRELSZ: jmpsz = d->d_un.d_val; break;
        case DT_PLTREL: pltrel = d->d_un.d_val; break;
#if defined(__LP64__)
        case DT_RELA: rel = d->d_un.d_ptr; rela = true; break;
        case DT_RELASZ: relsz = d->d_un.d_val; break;
#else
        case DT_REL: rel = d->d_un.d_ptr; break;
        case DT_RELSZ: relsz = d->d_un.d_val; break;
#endif
        }
    }
    auto fix = [&](ElfW(Addr) a) { return a && a < base ? a + base : a; };      // bionic leaves d_ptr unrelocated
    symtab = fix(symtab); strtab = fix(strtab); jmprel = fix(jmprel); rel = fix(rel);
    if (!symtab || !strtab) return 1;
    const ElfW(Sym)* syms = (const ElfW(Sym)*)symtab; const char* strs = (const char*)strtab;
    auto scan = [&](ElfW(Addr) tab, size_t sz, bool isRela) -> bool {
        if (!tab || !sz) return false;
        const size_t ent = isRela ? sizeof(ElfW(Rela)) : sizeof(ElfW(Rel));
        for (size_t off = 0; off + ent <= sz; off += ent)
        {
            const ElfW(Rel)* r = (const ElfW(Rel)*)(tab + off);       // r_offset / r_info are the first two fields of both
#if defined(__LP64__)
            const size_t si = ELF64_R_SYM(r->r_info);
#else
            const size_t si = ELF32_R_SYM(r->r_info);
#endif
            if (!si) continue;
            if (!strcmp(strs + syms[si].st_name, q->sym)) { q->slot = (void**)(base + r->r_offset); return true; }
        }
        return false;
    };
#if defined(__LP64__)
    const bool jmpRela = pltrel == 0 || pltrel == DT_RELA;
#else
    const bool jmpRela = pltrel == DT_RELA;
#endif
    if (!scan(jmprel, jmpsz, jmpRela)) scan(rel, relsz, rela);
    return 1;
}
static void** FindGot(const char* lib, const char* sym)
{
    GotQuery q{ lib, sym, nullptr };
    dl_iterate_phdr(GotCb, &q);
    return q.slot;
}
template <typename F> static bool HookGot(const char* sym, F hook, F* orig)
{
    void** slot = FindGot("libGTASA.so", sym);
    if (!slot || !*slot) return false;
    return aml->HookPLT((void*)slot, (void*)hook, (void**)orig) && *orig;
}
static void SetupBufferHooks()
{
    const bool d = HookGot("glBufferData", &h_glBufferData, &o_glBufferData);
    const bool x = HookGot("glDeleteBuffers", &h_glDeleteBuffers, &o_glDeleteBuffers);
    const bool s = HookGot("glBufferSubData", &h_glBufferSubData, &o_glBufferSubData);
    const bool m = HookGot("glMapBufferRange", &h_glMapBufferRange, &o_glMapBufferRange);
    Shadow_SetBufferTracking(d && x);
    PS_FileLog("shadows: buffer tracking %s (glBufferData %d, glDeleteBuffers %d, glBufferSubData %d, glMapBufferRange %d)%s", d && x ? "on" : "OFF", d, x, s, m,
               d && x ? "" : " - remembered casters only rely on buffer sizes");
}

DECL_HOOKp(RwCameraSetNearClipPlane, void* cam, float nearPlane)
{
    if (g_cfg.enabled && g_cfg.zfix) nearPlane = g_cfg.nearClip;
    return RwCameraSetNearClipPlane(cam, nearPlane);
}

// ---------------------------------------------------------------- 1.6: rename to TyFMCGFX
// files/ProperShader (<=1.3) and files/ProperGFX (1.4-1.5) -> files/TyFMCGFX, settings.ini / TYFMC.ProperGFX.ini -> TyFMCGFX.ini
static bool Exists(const char* p) { return access(p, F_OK) == 0; }
static bool IsOldIni(const char* n) { return !strcasecmp(n, "settings.ini") || !strcasecmp(n, "TYFMC.ProperGFX.ini"); }
static bool IsOldJunk(const char* n) { return !strncasecmp(n, "shadow_guard", 12) || !strcasecmp(n, "propergfx_log.txt"); }
static void MigrateOneFolder(const char* files, const char* oldName, const char* newDir)
{
    char oldDir[600]; snprintf(oldDir, sizeof(oldDir), "%s/%s", files, oldName);
    if (!Exists(oldDir)) return;
    if (!Exists(newDir) && rename(oldDir, newDir) == 0) { logger->Info("moved files/%s to files/" PS_DIR_NAME, oldName); return; }
    // both exist: move what is ours file by file (never overwrite), drop old guard / log files, remove the old folder when empty
    mkdir(newDir, 0777);
    if (DIR* d = opendir(oldDir))
    {
        while (dirent* e = readdir(d))
        {
            const char* n = e->d_name;
            if (n[0] == '.') continue;
            char from[900], to[900]; snprintf(from, sizeof(from), "%s/%s", oldDir, n);
            if (IsOldJunk(n)) { remove(from); continue; }
            snprintf(to, sizeof(to), "%s/%s", newDir, IsOldIni(n) ? PS_INI_NAME : n);
            if (!Exists(to)) rename(from, to);
        }
        closedir(d);
    }
    rmdir(oldDir);
}
static void MigrateOldFolder(const char* files, const char* newDir)
{
    MigrateOneFolder(files, "ProperGFX", newDir);
    MigrateOneFolder(files, "ProperShader", newDir);
    const char* oldInis[] = { "TYFMC.ProperGFX.ini", "settings.ini" };
    char newIni[700]; snprintf(newIni, sizeof(newIni), "%s/" PS_INI_NAME, newDir);
    for (const char* oi : oldInis)
    {
        char oldIni[700]; snprintf(oldIni, sizeof(oldIni), "%s/%s", newDir, oi);
        if (Exists(oldIni) && !Exists(newIni) && rename(oldIni, newIni) == 0) logger->Info("%s renamed to " PS_INI_NAME, oi);
        char junk[700]; snprintf(junk, sizeof(junk), "%s/propergfx_log.txt", newDir); remove(junk);
    }
}

// ---------------------------------------------------------------- SAUtils (optional): "TyFMCGFX" entries in the game's Settings > Mods tab
// Useful when the floating button is hidden or fully transparent.
static ISAUtils* sautils = nullptr;
static int g_sauButtonItem = -1;
static void OnSAUOpenMenu(uintptr_t) { UI_RequestOpen(); }
static void OnSAUButtonChanged(int, int nv, void*) { g_cfg.showButton = nv != 0; PS_SaveSettings(g_cfg, g_iniPath); }
static void SetupSAUtils()
{
    sautils = (ISAUtils*)GetInterface("SAUtils");
    if (!sautils) return;
    static const char* kOnOff[] = { "Off", "On", nullptr };
    sautils->AddButton(SetType_Mods, "TyFMCGFX - open menu", OnSAUOpenMenu);
    g_sauButtonItem = sautils->AddClickableItem(SetType_Mods, "TyFMCGFX - floating button", g_cfg.showButton ? 1 : 0, 0, 1, kOnOff, OnSAUButtonChanged);
    logger->Info("SAUtils found: TyFMCGFX entries added to Settings > Mods");
}

// ---------------------------------------------------------------- entry
extern "C" __attribute__((visibility("default"))) void OnModLoad()
{
    logger->SetTag("TyFMCGFX");
    void* hGame = aml->GetLibHandle("libGTASA.so");
    if (!hGame) { logger->Error("libGTASA.so not found"); return; }
    if (aml->HasMod("net.propershader.mobile")) { logger->Error("Proper Shader Mobile 1.2 (libProperShader32/64.so) is also installed - delete it, both cannot run together"); return; }

    const char* base = aml->GetAndroidDataPath();
    char files[512];
    snprintf(files, sizeof(files), "%s", base && base[0] ? base : "/sdcard/Android/data/com.rockstargames.gtasa/files");
    { size_t l = strlen(files); if (l > 1 && files[l - 1] == '/') files[l - 1] = 0; }
    snprintf(g_dir, sizeof(g_dir), "%s/" PS_DIR_NAME, files);
    MigrateOldFolder(files, g_dir);         // 1.3 and older: files/ProperShader/settings.ini -> files/TyFMCGFX/TyFMCGFX.ini
    mkdir(g_dir, 0777);
    {   // kill-switch: create TyFMCGFX/disable.txt to make the mod do nothing at all
        char kill[640]; snprintf(kill, sizeof(kill), "%s/disable.txt", g_dir);
        if (access(kill, F_OK) == 0) { logger->Info("disable.txt found - TyFMCGFX stays inactive"); return; }
    }
    snprintf(g_iniPath, sizeof(g_iniPath), "%s/" PS_INI_NAME, g_dir);
    if (!PS_LoadSettings(g_cfg, g_iniPath)) PS_SaveSettings(g_cfg, g_iniPath);
    UI_SetIniPath(g_iniPath);
    // 64-bit: every shader is a .glsl built into libTyFMCGFX64.so (nothing is read from files/); also removes the shaders folder 1.4.0 wrote
    GLSL_Init(g_dir);
    World_LoadSnippets();
    World_SetEnabled(g_cfg.enabled && g_cfg.worldPatch);
    World_SetCompileCel(g_cfg.celWorld);
    {   // shadow crash guard: the guard file exists from launch until shadows ran fine for a while; if it is still there next launch, shadows stay off (safe mode)
        {   // v1.2 used shadow_guard.txt with the broken 900-replay rule and left it behind on almost every launch: it must not put 1.3 into safe mode
            char legacy[640]; snprintf(legacy, sizeof(legacy), "%s/shadow_guard.txt", g_dir);
            if (access(legacy, F_OK) == 0) { remove(legacy); logger->Info("removed the leftover Proper Shader 1.2 shadow_guard.txt"); }
        }
        {   // v1.3.0 / 1.3.1 guard files: written by builds whose patched shaders could abort the game; that cause is fixed (self-test), so they must not keep shadows off
            char old13[640]; snprintf(old13, sizeof(old13), "%s/shadow_guard_v13.txt", g_dir);
            if (access(old13, F_OK) == 0) { remove(old13); logger->Info("removed the 1.3.0/1.3.1 shadow guard file"); }
        }
        {   // 1.3.2 - 1.4.4 guard file: 1.4.3 crashed at load (dlsym) AFTER writing it, so every later launch started in safe mode - drop it
            char old132[640]; snprintf(old132, sizeof(old132), "%s/shadow_guard_v132.txt", g_dir);
            if (access(old132, F_OK) == 0) { remove(old132); logger->Info("removed the old shadow_guard_v132.txt (left by the 1.4.3 start-up crash)"); }
        }
        snprintf(g_guardPath, sizeof(g_guardPath), "%s/shadow_guard_v145.txt", g_dir);
        snprintf(PS_LogFilePath(), 640, "%s/tyfmcgfx_log.txt", g_dir);
        { FILE* lf = fopen(PS_LogFilePath(), "rb"); if (lf) { fseek(lf, 0, SEEK_END); long sz = ftell(lf); fclose(lf); if (sz > 96 * 1024) remove(PS_LogFilePath()); } }
        PS_FileLog("---- launch: TyFMCGFX " PS_VERSION " (%d-bit) ----", PS_BITS);
        if (g_cfg.shadows && g_cfg.shadowGuard && access(g_guardPath, F_OK) == 0) { g_guardTripped = true; logger->Info("shadow_guard_v145.txt found: shadows are in SAFE MODE (previous launch did not finish). Menu > DynamicShadows > Retry shadows."); PS_FileLog("shadows: SAFE MODE (shadow_guard_v145.txt from the previous launch)"); }
        if (g_guardTripped) Shadow_SetGuardFailed(true);
        // 1.4.6: always Deferred - the game's shaders never get the 1.4.2 shadow receivers (like libZyZGFX64)
        g_cfg.shadowMethod = 0;
        World_SetCompileShadows(false);
    }

    SET_TO(g_pWetRoads,    aml->GetSym(hGame, "_ZN8CWeather8WetRoadsE"));
    SET_TO(g_pUnderWater,  aml->GetSym(hGame, "_ZN8CWeather14UnderWaternessE"));
    SET_TO(g_pVectorToSun, aml->GetSym(hGame, "_ZN10CTimeCycle13m_VectorToSunE"));
    SET_TO(g_pSunIndex,    aml->GetSym(hGame, "_ZN10CTimeCycle20m_CurrentStoredValueE"));
    SET_TO(g_pCamera,      aml->GetSym(hGame, "TheCamera"));
    SET_TO(g_pFov,         aml->GetSym(hGame, "_ZN5CDraw7ms_fFOVE"));
    SET_TO(g_pAspect,      aml->GetSym(hGame, "_ZN5CDraw15ms_fAspectRatioE"));
    SET_TO(g_pClockH,      aml->GetSym(hGame, "_ZN6CClock18ms_nGameClockHoursE"));
    SET_TO(g_pClockM,      aml->GetSym(hGame, "_ZN6CClock20ms_nGameClockMinutesE"));
    SET_TO(g_pShdwTable,   aml->GetSym(hGame, "_ZN10CTimeCycle17m_nShadowStrengthE"));
    SET_TO(g_pWeatherOld,  aml->GetSym(hGame, "_ZN8CWeather14OldWeatherTypeE"));
    SET_TO(g_pWeatherNew,  aml->GetSym(hGame, "_ZN8CWeather14NewWeatherTypeE"));
    SET_TO(g_pWeatherInterp, aml->GetSym(hGame, "_ZN8CWeather18InterpolationValueE"));
    SET_TO(g_pTimeMs,      aml->GetSym(hGame, "_ZN6CTimer22m_snTimeInMillisecondsE"));
    SET_TO(g_pModelInfo,   aml->GetSym(hGame, "_ZN10CModelInfo16ms_modelInfoPtrsE"));
    SET_TO(g_pUserPause,   aml->GetSym(hGame, "_ZN6CTimer11m_UserPauseE"));
    SET_TO(g_pCodePause,   aml->GetSym(hGame, "_ZN6CTimer11m_CodePauseE"));
    SET_TO(g_pRain,        aml->GetSym(hGame, "_ZN8CWeather4RainE"));
    SET_TO(g_findPlayerVehicle, aml->GetSym(hGame, "_Z17FindPlayerVehicleib"));
    SET_TO(g_pLightning,   aml->GetSym(hGame, "_ZN8CWeather14LightningFlashE"));
    SET_TO(g_pStreamMem,   aml->GetSym(hGame, "_ZN10CStreaming18ms_memoryAvailableE"));
    if (g_pStreamMem && g_cfg.streamingMB > 0) *g_pStreamMem = (unsigned)g_cfg.streamingMB * 1024u * 1024u;
    // 1.6.0 light sources (ZyZGfx 2.0: HookOf_DlStoreStatic / HookOf_DlCarLightShadow / point lights / HookOf_BmHeadLightBeam)
    if (aml->GetSym(hGame, "_ZN8CShadows17StoreStaticShadowEyhP9RwTextureP7CVectorffffshhhfffbf")) HOOKSYM(StoreStaticShadow, hGame, "_ZN8CShadows17StoreStaticShadowEyhP9RwTextureP7CVectorffffshhhfffbf");
    if (aml->GetSym(hGame, "_ZN8CShadows19StoreCarLightShadowEP8CVehicleiP9RwTextureP7CVectorffffhhhf")) HOOKSYM(StoreCarLightShadow, hGame, "_ZN8CShadows19StoreCarLightShadowEP8CVehicleiP9RwTextureP7CVectorffffhhhf");
    if (aml->GetSym(hGame, "_ZN12CPointLights8AddLightEh7CVectorS0_ffffhbP7CEntity")) HOOKSYM(CPointLights_AddLight, hGame, "_ZN12CPointLights8AddLightEh7CVectorS0_ffffhbP7CEntity");
    if (aml->GetSym(hGame, "_ZN8CVehicle15DoHeadLightBeamEiR7CMatrixh")) HOOKSYM(DoHeadLightBeam, hGame, "_ZN8CVehicle15DoHeadLightBeamEiR7CMatrixh");
    PS_FileLog("lights: static %s, car %s, point %s, beams %s, lightning %s, streaming %s",
               aml->GetSym(hGame, "_ZN8CShadows17StoreStaticShadowEyhP9RwTextureP7CVectorffffshhhfffbf") ? "ok" : "MISSING",
               aml->GetSym(hGame, "_ZN8CShadows19StoreCarLightShadowEP8CVehicleiP9RwTextureP7CVectorffffhhhf") ? "ok" : "MISSING",
               aml->GetSym(hGame, "_ZN12CPointLights8AddLightEh7CVectorS0_ffffhbP7CEntity") ? "ok" : "MISSING",
               aml->GetSym(hGame, "_ZN8CVehicle15DoHeadLightBeamEiR7CMatrixh") ? "ok" : "MISSING", g_pLightning ? "ok" : "MISSING", g_pStreamMem ? "ok" : "MISSING");
    Shadow_SetGameCamera((const void*)aml->GetSym(hGame, "TheCamera"));
    Shadow_SetAlphaTestState((const int*)aml->GetSym(hGame, "_ZZ25RQ_Command_rqSetAlphaTestRPcE7curFunc"),
                             (const float*)aml->GetSym(hGame, "_ZZ25RQ_Command_rqSetAlphaTestRPcE6curVal"));
    PS_FileLog("shadows: alpha-test state %s, model info %s, user pause %s", aml->GetSym(hGame, "_ZZ25RQ_Command_rqSetAlphaTestRPcE7curFunc") ? "found" : "MISSING",
               g_pModelInfo ? "found" : "MISSING", g_pUserPause ? "found" : "MISSING");

    HOOKSYM(OS_ScreenSwapBuffers,     hGame, "_Z20OS_ScreenSwapBuffersv");
    HOOKSYM(AND_TouchEvent,           hGame, "_Z14AND_TouchEventiiii");
    HOOKSYM(RwCameraSetNearClipPlane, hGame, "_Z24RwCameraSetNearClipPlaneP8RwCameraf");
    if (aml->GetSym(hGame, "_Z23RwCameraSetFarClipPlaneP8RwCameraf")) HOOKSYM(RwCameraSetFarClipPlane, hGame, "_Z23RwCameraSetFarClipPlaneP8RwCameraf");
    if (aml->GetSym(hGame, "_ZN7CEntity6RenderEv")) HOOKSYM(CEntity_Render, hGame, "_ZN7CEntity6RenderEv");
    Scene_SetHandler(OnSceneEvent);
    if (aml->GetSym(hGame, "_ZN12CPostEffects12MobileRenderEv")) { HOOKSYM(CPostEffects_MobileRender, hGame, "_ZN12CPostEffects12MobileRenderEv"); g_postHooked = true; }
    PS_FileLog("post effects: %s", g_postHooked ? "in-frame before CPostEffects::MobileRender (menus / HUD untouched)" : "in-frame after RenderScene (CPostEffects::MobileRender not found)");
    PSApi_Init(&g_cfg, g_dir, g_iniPath);
    RegisterInterface("TyFMCGFX", PSApi_Get());
    RegisterInterface("ProperGFX", PSApi_Get());   // 1.5 name, kept for mods that already use it
    static void* s_hGame; s_hGame = hGame;
    if (aml->GetSym(hGame, "_Z11RenderSceneb")) { HOOKSYM(RenderScene, hGame, "_Z11RenderSceneb"); }   // always: also the gameplay detection
    else PS_FileLog("RenderScene not found - effects / menu button are shown everywhere (no gameplay detection)");
    g_sceneHooked = aml->GetSym(hGame, "_Z11RenderSceneb") != 0;
    SetupBufferHooks();
    if (Scene_Init([](const char* n) -> void* { return (void*)aml->GetSym(s_hGame, n); }))
    {
        if (aml->GetSym(hGame, "_Z25RQ_Command_rqTargetSelectRPc")) HOOKSYM(RQ_Command_rqTargetSelect, hGame, "_Z25RQ_Command_rqTargetSelectRPc");
        PS_FileLog("scene: render-queue symbols found, RenderScene %s, rqTargetSelect %s", aml->GetSym(hGame, "_Z11RenderSceneb") ? "hooked" : "MISSING",
                   aml->GetSym(hGame, "_Z25RQ_Command_rqTargetSelectRPc") ? "hooked" : "MISSING");
    }
    else PS_FileLog("%s", Scene_Stats().note);
    HOOKSYM(RQ_Command_rqDrawIndexed, hGame, "_Z24RQ_Command_rqDrawIndexedRPc");   // always (1.4.5): free while not capturing, lets Medium/High switch shadows on live even after a launch on Low
    if (g_cfg.worldPatch)
    {
        HOOKSYM(ES2Shader_Build,          hGame, "_ZN9ES2Shader5BuildEPKcS1_");        // installed before the source hook: nothing patched may reach the game untested
        HOOKSYM(RQShader_BuildSource,     hGame, "_ZN8RQShader11BuildSourceEjPPKcS2_");
        HOOKSYM(RQ_Command_rqSelectShader, hGame, "_Z25RQ_Command_rqSelectShaderRPc");
        HOOKSYM(RQ_Command_rqDeleteShader, hGame, "_Z25RQ_Command_rqDeleteShaderRPc");
    }
    SetupSAUtils();
    logger->Info("TyFMCGFX " PS_VERSION " loaded (%d-bit). Config: %s", PS_BITS, g_iniPath);
}
