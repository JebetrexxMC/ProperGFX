// TyFMCGFX (formerly Proper GFX Mobile / Proper Shader Mobile) - settings (files/TyFMCGFX/TyFMCGFX.ini).
// Modelled on how the PC mod is configured: one section per feature, each with an Enabled switch,
// separate Day / Night values for the parameters that matter, and Low / Medium / High presets.
#pragma once
#include <stdint.h>
#include <stddef.h>

#define PS_VERSION     "1.6.2"
#define PS_DIR_NAME    "TyFMCGFX"               // files/TyFMCGFX (1.4-1.5: files/TyFMCGFX, 1.3 and older: files/ProperShader - moved automatically)
#define PS_INI_NAME    "TyFMCGFX.ini"           // 1.4-1.5: TyFMCGFX.ini, 1.3 and older: settings.ini (renamed automatically)
#ifndef PS_BUTTON_MIN_OPACITY
#define PS_BUTTON_MIN_OPACITY 0.05f             // 32-bit build keeps the 1.3 range; the 64-bit build allows a fully transparent button (0.0)
#endif

enum PSTonemap { TM_OFF = 0, TM_ACES = 1, TM_TIMOTHY = 2, TM_REINHARD = 3, TM_AGX = 4, TM_NEUTRAL = 5 };
enum PSQuality { Q_LOW = 0, Q_MEDIUM = 1, Q_HIGH = 2, Q_CUSTOM = 3 };

// A value with a Day and a Night setting; `v` is the live (time-of-day blended) value used by the renderer.
struct DN
{
    float day, night, v;
    DN(float d = 0.f, float n = 0.f) : day(d), night(n), v(d) {}
};

struct PSSettings
{
    // ---- [General] ----
    bool  enabled      = true;
    bool  postFx       = true;
    bool  worldPatch   = true;       // object-shader patches (needs a game restart to (un)install)
    int   quality      = Q_MEDIUM;   // Low / Medium / High / Custom
    int   look         = 0;          // see PS_LookNames
    bool  adaptive     = true;       // adaptive quality governor
    int   targetFps    = 0;          // 0 = 30, 1 = 60
    int   timeOfDay    = 0;          // 0 auto (game clock), 1 force day, 2 force night

    // ---- [SimulateHDR] ----
    bool  hdr          = true;
    int   tonemap      = TM_ACES;
    DN    hdrAmount      { 0.55f, 0.45f };
    DN    exposure       { 0.00f, 0.20f };
    // ---- [AutoExposure] ----
    bool  autoExp      = true;
    float autoBias     = 0.f, autoRange = 1.f, autoSpeed = 1.2f;
    // ---- [Bloom] ----
    bool  bloom        = true;
    DN    bloomIntensity { 0.35f, 0.50f };
    float bloomThreshold = 0.62f, bloomRadius = 1.0f;
    // ---- [SunRays] / [LensFlare] ----
    bool  sunRays      = false;
    float sunRaysIntensity = 0.45f, sunRaysDecay = 0.93f;
    bool  lensFlare    = false;
    float lensFlareIntensity = 0.35f;
    // ---- [AntiAliasing] / [Sharpen] ----
    bool  fxaa         = true;
    float fxaaStrength = 0.85f;
    bool  cas          = true;
    float casStrength  = 0.45f;
    // ---- [ColorGrading] ----
    bool  grading      = true;
    int   gradePreset  = 0;
    float gradeAmount  = 1.0f;
    DN    contrast       { 1.05f, 1.05f };
    DN    saturation     { 1.08f, 1.00f };
    float vibrance = 0.20f, temperature = 0.f, tint = 0.f, gamma = 1.f, lift = 0.f, gain = 1.f;
    // ---- [LUT] colour lookup texture: files/TyFMCGFX/<LUTFile> (png / bmp / tga / jpg; 256x16, 1024x32, 4096x64, 64x64, 512x512) ----
    bool  lutOn        = true;
    float lutAmount    = 1.0f;       // 1.4: default 1 (was 0, so a dropped-in LUT seemed to do nothing)
    char  lutFile[64]  = "auto";     // "auto" = the first lut.png / lut.bmp / lut.tga / lut.jpg (any case) in files/TyFMCGFX or files/TyFMCGFX/luts
    // ---- [Dithering] / [Vignette] / [FilmGrain] / [ChromaticAberration] ----
    bool  dither       = true;
    bool  vignette     = true;
    DN    vignetteStrength { 0.30f, 0.40f };
    float vignetteSize = 0.55f;
    bool  grain        = false;
    float grainStrength = 0.25f;
    bool  chromatic    = false;
    float chromaticStrength = 0.35f;
    // ---- [DepthOfField] (tilt-shift, no depth buffer on mobile) ----
    bool  tiltShift    = false;
    float tiltFocus = 0.5f, tiltWidth = 0.22f, tiltStrength = 0.8f;
    int   dofMode      = 2;          // 1.6.1: 0 = tilt-shift band, 1 = center focus (circle), 2 = real depth (camera depth pass, ps_shadow.cpp)
    float dofFocusDist = 0.f;        // manual focus distance in metres, 0 = auto focus
    float dofAperture  = 1.0f;       // more = shallower depth of field
    float dofBokeh     = 0.6f;
    float dofRadius    = 1.0f;
    // ---- [Underwater] ----
    bool  underwater   = true;
    float underwaterStrength = 1.0f;
    // ---- [HorizonFog] / [SurfaceShading] / [WorldRainfall] / [WaterSurface] ----
    bool  fogOn        = true;
    DN    fogDensity     { 1.0f, 1.0f };
    float fogSoftness  = 0.35f;      // -0.5 harder .. +1 softer (no pow() in the shader)
    bool  surfaceOn    = true;
    float specularMul  = 1.0f, reflectionMul = 1.0f;
    bool  wetWorld     = true;
    float wetStrength  = 1.0f;
    bool  detailOn     = true;       // 1.6.1 [DetailTextures]: the diffuse texture again at 2x tiling as micro detail + relief shading
    float detailStrength = 0.5f;
    float detailRelief = 0.4f;
    bool  enhance      = false;      // 1.6.1 [VisualEnhance]: edge-aware denoise + clarity (own algorithm, runs on any GPU)
    float enhanceClarity = 0.45f;
    float enhanceDenoise = 0.25f;
    float enhanceRadius = 2.5f;
    bool  sunShadowsOn = true; float sunShadowLength = 1.f, sunShadowWidth = 1.f; bool sunShadowLowLong = true;   // 1.6.2 [SunShadows]
    float tmStrength = 1.f, tmWhite = 1.f, tmContrast = 1.f, tmSaturation = 1.f;   // 1.6.2 [SimulateHDR] tone-mapping controls
    // 1.6.2 prod80 ports (MIT): [ColorBalance] shadows / midtones / highlights RGB shift, [Technicolor] 3-strip film look
    bool  cbOn = false; float cbS[3] = { -0.10f, 0.0f, 0.12f }, cbM[3] = { 0.f, 0.f, 0.f }, cbH[3] = { 0.10f, 0.03f, -0.08f };
    bool  techOn = false; float techStrength = 0.5f, techColor = 0.2f, techBright = 1.0f, techSat = 1.0f;
    bool  perfOverlay  = false;      // 1.6.1 [PerformanceOverlay]: FPS, 1% low, frame-time graph, CPU temperature, free RAM
    int   perfCorner   = 1;          // 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right
    bool  waterOn      = true;
    float waterDetail  = 1.0f;
    // ---- [ImprovedZFighting] ----
    bool  zfix         = false;
    float nearClip     = 0.30f;
    // ---- [DynamicShadows] sun shadow map (buildings, trees, vehicles cast onto the world) ----
    bool  shadows      = true;
    float shadowStrength = 0.55f;
    int   shadowRes    = 1;          // 0 = 512, 1 = 1024, 2 = 2048
    float shadowDist   = 80.f;       // metres around the camera covered by the map
    int   shadowEvery  = 3;          // re-render the map every N frames (world is static, sun moves slowly)
    float shadowSoft   = 1.0f;       // PCF radius multiplier
    float shadowBias   = 1.0f;       // bias multiplier
    float shadowMinSun = 0.05f;      // sun elevation (sin) below which shadows fade out (dusk/night)
    float sunSmoothing = 0.06f;      // low-pass on the sun direction (clock-mod steps)
    bool  shadowDebug  = false;      // overlay the shadow map in a corner
    int   sunSource    = 0;          // 0 = Auto (game's time-cycle sun vector, falls back to the light uniform), 1 = time-cycle sun only, 2 = light uniform only
    bool  shadowTimecyc = true;      // shadow strength follows timecyc.dat (the 'Shdw' column) by hour and weather
    bool  shadowGuard  = true;       // crash guard: shadows switch themselves off after a launch that did not survive
    bool  shadowRetry  = false;      // runtime only (menu button): clear the crash guard
    // ---- artistic ----
    bool  cel          = false;
    float celLevels = 5.f, celAmount = 0.85f;
    bool  celWorld     = false;      // read when a shader is compiled (restart to change)
    float celWorldLevels = 4.f;
    bool  outline      = false;
    float outlineStrength = 0.8f, outlineThreshold = 0.22f;
    bool  vhs          = false;
    float vhsStrength  = 0.6f;
    // ---- [Button] / [Menu] ----
    bool  showButton   = true;
    float buttonX = 0.38f, buttonY = 0.95f, buttonScale = 1.0f, buttonOpacity = 0.55f;
    bool  buttonLock   = false;      // [Button] Lock: the floating button can not be dragged (1.6.0)
    bool  buttonCollapse = true;     // shrink to a small dot when idle so it never covers other mods' buttons
    float uiScale      = 1.0f;
};

extern uint32_t g_worldGen;                 // bumped whenever an object-shader uniform may have changed
extern const char* const PS_QualityNames[]; extern const int PS_QualityCount;
extern const char* const PS_LookNames[];    extern const int PS_LookCount;
extern const char* const PS_GradeNames[];   extern const int PS_GradeCount;
extern const char* const PS_DofModeNames[];   extern const int PS_DofModeCount;
extern const char* const PS_DofModeUiNames[];
extern const char* const PS_TonemapNames[]; extern const int PS_TonemapCount;
extern const char* const PS_TimeNames[];    extern const int PS_TimeCount;
extern const char* const PS_SunSourceNames[]; extern const int PS_SunSourceCount;

bool  PS_LoadSettings(PSSettings& s, const char* path);
bool  PS_SaveSettings(const PSSettings& s, const char* path);
void  PS_ResetSettings(PSSettings& s);
void  PS_ApplyQuality(PSSettings& s, int quality);   // Low / Medium / High
void  PS_ApplyLook(PSSettings& s, int look);         // colour/mood looks on top of a quality tier
float PS_NightFactor(int hours, int minutes);        // 0 = full day .. 1 = full night
void  PS_UpdateLive(PSSettings& s, float nightFactor);
