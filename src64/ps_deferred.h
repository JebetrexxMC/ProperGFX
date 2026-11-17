// Proper GFX Mobile 1.5.0 (64-bit) - deferred screen-space lighting.
//
// GTA SA Mobile has a forward renderer (no G-buffer). Like libZyZGFX64, Proper GFX gives the game's scene target a readable depth
// texture (ps_scene.cpp) and records the frame's camera (ps_shadow.cpp). With both, every pixel's world position and normal are rebuilt
// right after RenderScene, and these passes run on the 3D world only - before the game draws its HUD, radar, menus and pause menu:
//
//   def_ao.glsl        [AmbientOcclusion] SSAO / HBAO / SSGI (+ def_bilateral.glsl depth-aware blur)
//   def_volume.glsl    [VolumetricFog] height fog + god rays through the sun shadow cascade, [VolumetricHeadlights]
//   def_composite.glsl AO, GI, [Skylight], [AmbientLighting] tricolour ambient, [ContactShadows], [SSR], [WorldRain] puddles + ripples,
//                      [WaterCaustics], sky: [PBRSky], [VolumetricClouds], [Aurora], [RainFall] streaks
//   def_focus.glsl + def_coc.glsl   [DepthOfField] Mode = Adaptive (auto focus, circle of confusion for fragment/dof.glsl)
//   def_taa.glsl, def_fsr.glsl      [TAA] and [FSR] RCAS, after the post effects (still before the HUD)
#pragma once
#include "ps_settings.h"
#include <GLES2/gl2.h>

struct PSDeferredFrame
{
    float time = 0.f, dt = 0.016f, night = 0.f, rain = 0.f, wet = 0.f, underwater = 0.f;
    float sun[3] = {0.f, 0.f, 1.f};           // toward the sun (CTimeCycle::m_VectorToSun)
    bool  headlights = false;                  // player's vehicle at night
    float hl0[3] = {0, 0, 0}, hl1[3] = {0, 0, 0}, hlDir[3] = {0, 1, 0};   // world positions + direction of the two lamps
};

struct PSDeferredStats
{
    bool  es3 = false, ready = false, failed = false;
    uint32_t frames = 0, noCamera = 0;
    int   passes = 0;
    bool  ao = false, vol = false, composite = false, coc = false, taa = false, fsr = false;
    char  note[128] = "";
};

bool   Deferred_Wanted(const PSSettings& s);              // needs the camera / scene depth (game thread or render thread)
bool   Deferred_PostWanted(const PSSettings& s);          // TAA / FSR
void   Deferred_SetFrame(const PSDeferredFrame& f);
void   Deferred_AfterScene(const PSSettings& s, int perfLevel);   // render thread, after-scene event, after the shadow mask; GL state saved by the caller
void   Deferred_PostAA(const PSSettings& s, GLuint fbo, int w, int h); // render thread, after FX_Render on the same target
GLuint Deferred_CocTexture();                               // 0 = no adaptive DoF this frame
void   Deferred_ContextLost();
void   Deferred_Shutdown();
const PSDeferredStats& Deferred_Stats();
