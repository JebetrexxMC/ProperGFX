// TyFMCGFX (formerly Proper GFX Mobile / Proper Shader Mobile) - settings (files/TyFMCGFX/TyFMCGFX.ini).
// Modelled on how the PC mod is configured: one section per feature, each with an Enabled switch,
// separate Day / Night values for the parameters that matter, and Low / Medium / High presets.
#pragma once
#include <stdint.h>
#include <stddef.h>

#define PS_VERSION     "1.6.2"
#define PS_DIR_NAME    "TyFMCGFX"               // files/TyFMCGFX (1.4-1.5: files/TyFMCGFX, 1.3 and older: files/ProperShader - moved automatically)
#define PS_INI_NAME    "TyFMCGFX.ini"           // 1.4-1.5: TyFMCGFX.ini, 1.3 and older: settings.ini (renamed automatically)
#define PS_BUTTON_HIDDEN_BELOW 0.06f            // 64-bit: anything below = 0 = invisible (an old 0.05 from the 32-bit range included)
#define PS_BUTTON_MIN_OPACITY 0.0f              // 64-bit: Opacity 0 = button label, fill AND outline fully transparent (32-bit build keeps 0.05)

enum PSTonemap { TM_OFF = 0, TM_ACES = 1, TM_TIMOTHY = 2, TM_REINHARD = 3, TM_AGX = 4, TM_NEUTRAL = 5 };
enum PSQuality { Q_LOW = 0, Q_MEDIUM = 1, Q_HIGH = 2, Q_CUSTOM = 3, Q_ULTRA = 4 };   // 1.5: Ultra added after Custom (old ini values keep their meaning)
enum PSAoMode { AO_SSAO = 0, AO_HBAO = 1, AO_SSGI = 2 };

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
    char  lutFolder[128] = "";       // [LUT] Folders = directory of the LUT: empty = files/TyFMCGFX (+ /luts); relative to files/TyFMCGFX, or an absolute path
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
    int   dofMode      = 2;          // 0 = tilt-shift band, 1 = center focus (circle), 2 = real depth (1.6.1 default)   (fragment/dof.glsl)
    float dofFocusDist = 0.f;        // 1.6.1: manual focus distance in metres, 0 = auto focus (what is in the screen centre)
    float dofAperture  = 1.0f;       // 1.6.1: lens aperture - more = shallower depth of field
    float dofBokeh     = 0.6f;       // bright out-of-focus taps weigh more -> round highlights
    float dofRadius    = 1.0f;       // blur radius multiplier
    float dofUnderwater = 0.5f;      // all-over blur while the camera is underwater (UnderWaterness)
    // ---- [SunVector] glow + golden hour from the game's sun vector (fragment/sunvector.glsl) ----
    bool  sunVectorOn  = true;
    float sunGlow      = 0.30f;      // glow disc around the sun
    float sunGlowSize  = 0.18f;
    float goldenHour   = 0.45f;      // warm picture while the sun is low
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
    int   shadowMethod = 0;          // 0 = Deferred (screen-space mask from the scene depth, the libZyZyGFX64 way), 1 = In-shader (1.4.2 receivers)
    bool  shadowMaskHalf = false;    // deferred: build the mask at half resolution (faster, slightly softer edges)
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
    float shadowTint   = 0.35f;      // colour of the shadowed part (fragment/shadows.glsl, PS_ShadowTint): 0 = plain darkening
    float shadowTintR = 0.80f, shadowTintG = 0.88f, shadowTintB = 1.0f;   // sky-blue ambient
    // ---- artistic ----
    bool  cel          = false;
    float celLevels = 5.f, celAmount = 0.85f;
    bool  celWorld     = false;      // read when a shader is compiled (restart to change)
    float celWorldLevels = 4.f;
    bool  outline      = false;
    float outlineStrength = 0.8f, outlineThreshold = 0.22f;
    bool  vhs          = false;
    float vhsStrength  = 0.6f;
    // ---- [GLSL] (shaders are built into the .so) ----
    bool  glslCustom   = false;      // run fragment/custom.glsl as the last pass
    float glslParam[8] = { 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };   // PGFX_User0 / PGFX_User1
    // ---- 1.5.0 deferred screen-space lighting (ps_deferred.cpp + shaders/fragment/def_*.glsl, GLSL ES 3.00).
    //      Runs on the render thread right after RenderScene (render-queue event), on the game's scene target with its readable depth:
    //      HUD, radar, menus, pause menu and the floating button are drawn later and are never touched.
    bool  deferredOn   = true;       // [Deferred] Enabled - master switch of everything in this block
    bool  deferredHalf = true;       // [Deferred] HalfRes - AO / GI / volumetrics at half resolution (bilateral upsample)
    bool  aoOn         = true;       // [AmbientOcclusion]
    int   aoMode       = AO_HBAO;    // SSAO / HBAO / SSGI (HBAO + one bounce of indirect colour)
    float aoRadius     = 1.2f, aoIntensity = 0.85f, giStrength = 0.6f;
    bool  skylightOn   = true;       // [Skylight] sky occlusion: big-scale AO that only occludes the sky ambient colour
    float skylightStrength = 0.55f;
    bool  ambientOn    = true;       // [AmbientLighting] tricoloured ambient (sky / horizon / ground by the reconstructed normal)
    float ambientStrength = 0.30f;
    bool  contactOn    = true;       // [ContactShadows] short screen-space rays toward the sun
    float contactLength = 0.8f, contactStrength = 0.6f;
    bool  volFogOn     = true;       // [VolumetricFog] height fog with god rays (ray-marched through the sun shadow map)
    DN    volFogDensity { 0.30f, 0.45f };
    float volFogFalloff = 0.35f, volGodrays = 0.8f;
    bool  headlightsOn = false;      // [VolumetricHeadlights] experimental: light cones of the player's vehicle at night
    float headlightStrength = 0.6f;
    bool  cloudsOn     = false;      // [VolumetricClouds] ray-marched cloud layer over the sky pixels
    float cloudCoverage = 0.45f, cloudDensity = 0.8f, cloudSpeed = 1.0f;
    bool  pbrSkyOn     = false;      // [PBRSky] Rayleigh / Mie sky from the sun vector, blended over the game's sky
    float pbrSkyBlend  = 0.45f;
    bool  auroraOn     = false;      // [Aurora] borealis at night (sky pixels)
    float auroraStrength = 0.6f;
    bool  ssrOn        = true;       // [SSR] screen-space reflections on wet / flat / shiny surfaces
    float ssrStrength  = 0.55f, ssrDry = 0.12f;
    bool  rainSurfOn   = true;       // [WorldRain] puddles + ripples on upward surfaces while it rains (CWeather::WetRoads)
    float puddles      = 0.6f, ripples = 0.6f;
    bool  mudOn        = true;       // 1.6.2 [MudPuddles] muddy water, varied puddle sizes, rain splashes, water running down walls
    float mudAmount    = 0.55f, puddleVariety = 0.7f, splashes = 0.6f, drips = 0.5f;
    bool  rainFallOn   = true;       // [RainFall] screen-space falling rain streaks
    float rainFall     = 0.5f;
    bool  causticsOn   = true;       // [WaterCaustics] projected onto the sea floor (world position from depth)
    float causticsStrength = 0.6f;
    bool  taaOn        = false;      // [TAA] temporal anti-aliasing (reprojection with the scene depth, neighbourhood clamp)
    float taaBlend     = 0.86f;
    bool  fsrOn        = false;      // [FSR] FidelityFX Super Resolution 1 RCAS sharpening + edge reconstruction (EASU-style)
    float fsrSharpness = 0.35f;
    bool  shadowPcss   = true;       // [DynamicShadows] PCSS: contact-hardening penumbra (blocker search)
    bool  shadowBlurOn = true;       // [DynamicShadows] soft shadow mask: depth-aware gaussian blur of the mask
    int   shadowCascades = 2;        // [DynamicShadows] 2 or 4 cascades
    // ---- 1.6.0: features of ZyZGfx 2.0 (found with Ghidra in libZyZGfx64.so 2.0.7, ported to the deferred pipeline) ----
    bool  dynLightsOn  = true;       // [DynamicLights] street lamps / neon / car lamps / point lights shade the world (CShadows + CPointLights hooks)
    float dynLightStrength = 1.0f;
    float dynLightRange = 1.0f;      // radius multiplier
    bool  dynLightShadows = true;    // screen-space shadows for the 4 nearest lights
    bool  dynLightDebug = false;     // [DynamicLights] ShadowDebug: tint every light's area
    bool  replaceGameShadows = false;// skip the game's own additive light pools (CShadows::StoreStaticShadow type 2)
    bool  beamsOn      = true;       // [VolumetricLights] headlight beams of every car (CVehicle::DoHeadLightBeam)
    float beamStrength = 0.8f;
    bool  replaceGameBeams = false;  // skip the game's flat beam polygons
    bool  glareOn      = true;       // [LensGlare] star streaks on bright lights
    float glareStrength = 0.6f;
    bool  wetStreaksOn = true;       // [WetLightStreaks] long vertical reflections of lights on wet roads
    float wetStreaks   = 0.8f;
    bool  moonLightOn  = true;       // [Moon] directional moon light at night
    float moonLight    = 0.5f;
    bool  moonShadows  = true;       // the dynamic shadows follow the moon at night
    float moonShadowStrength = 0.55f;
    bool  moonRaysOn   = true;       // god rays from the moon through the volumetric fog
    float moonRays     = 0.5f;
    bool  moonGlitterOn = true;      // moon glitter on the sea
    float moonGlitter  = 0.6f;
    bool  nightLookOn  = true;       // [NightLook]
    float nightContrast = 1.08f, nightExposure = 0.15f, nightColourShift = 0.35f;
    float nightBloom   = 0.3f;       // extra bloom at night
    float lampGlow     = 0.5f, neonGlow = 0.45f, fogGlow = 0.5f;
    bool  cityGlowOn   = true;       // [NightSky] orange city light on the night sky and the clouds
    float cityGlow     = 0.5f;
    bool  starsOn      = true;       // stars + Milky Way + shooting stars on the night sky
    float stars        = 0.7f, milkyWay = 0.5f, shootingStars = 0.5f;
    bool  aoMultiBounce = true;      // [AmbientOcclusion] MultiBounce (Jimenez GTAO multi-bounce approximation)
    int   giQuality    = 1;          // [AmbientOcclusion] GIQuality 0 low / 1 medium / 2 high (sample count)
    bool  lightningOn  = true;       // [Weather] Lightning: flashes light up the world (CWeather::LightningFlash)
    float lightning    = 0.8f;
    bool  dryingOn     = true;       // [Weather] Drying: roads dry slowly after the rain (seconds)
    float dryingTime   = 90.f;
    bool  carRainOn    = true;       // [RainOnCars] beads + wet paint on vehicles while it rains (vehicle shader patch)
    int   carRainQuality = 1;        // 0 medium / 1 high / 2 ultra (bead density)
    float drawDistance = 1.0f;       // [Performance] DrawDistance multiplier (RwCameraSetFarClipPlane, 50 < far < 5000 like ZyZ)
    int   streamingMB  = 0;          // [Performance] StreamingMemory (MB, 0 = game default; CStreaming::ms_memoryAvailable at load)
    bool  pauseInMenu  = false;      // [Menu] PauseGame while the menu is open
    bool  buttonLock   = false;      // [Button] Lock: the floating button can not be dragged
    bool  captureNow   = false;      // transient: menu > Capture Frame
    // ---- [Button] / [Menu] ----
    bool  showButton   = true;
    float buttonX = 0.38f, buttonY = 0.95f, buttonScale = 1.0f, buttonOpacity = 0.55f;
    bool  buttonCollapse = true;     // shrink to a small dot when idle so it never covers other mods' buttons
    float uiScale      = 1.0f;
};

extern uint32_t g_worldGen;                 // bumped whenever an object-shader uniform may have changed
extern const char* const PS_QualityNames[]; extern const int PS_QualityCount;
extern const char* const PS_LookNames[];    extern const int PS_LookCount;
extern const char* const PS_GradeNames[];   extern const int PS_GradeCount;
extern const char* const PS_TonemapNames[]; extern const int PS_TonemapCount;
extern const char* const PS_TimeNames[];    extern const int PS_TimeCount;
extern const char* const PS_SunSourceNames[]; extern const int PS_SunSourceCount;
extern const char* const PS_ShadowMethodNames[]; extern const int PS_ShadowMethodCount;
// [LUT] Folders -> the directory LUT_Load searches (base = files/TyFMCGFX)
void PS_LutDir(const PSSettings& s, const char* base, char* out, int n);
extern const char* const PS_DofModeNames[];   extern const int PS_DofModeCount;
extern const char* const PS_DofModeUiNames[];
extern const char* const PS_AoModeNames[];    extern const int PS_AoModeCount;
extern const char* const PS_QualityMenuNames[]; // Low / Medium / High / Ultra (slider order)
int   PS_QualityToSlider(int q);                  // -1 = Custom
int   PS_SliderToQuality(int i);
bool  PS_PresetSave(const PSSettings& s, const char* dir, int slot);   // files/TyFMCGFX/presets/preset<slot>.ini
bool  PS_PresetLoad(PSSettings& s, const char* dir, int slot);
bool  PS_PresetDelete(const char* dir, int slot);
bool  PS_PresetExists(const char* dir, int slot);
// 1.5.0 public API helpers: one ini value by section + key (keys may end in .Day / .Night)
bool  PS_SetValue(PSSettings& s, const char* section, const char* key, const char* value);
bool  PS_GetValue(const PSSettings& s, const char* section, const char* key, char* out, int outLen);

bool  PS_LoadSettings(PSSettings& s, const char* path);
bool  PS_SaveSettings(const PSSettings& s, const char* path);
void  PS_ResetSettings(PSSettings& s);
void  PS_ApplyQuality(PSSettings& s, int quality);   // Low / Medium / High
void  PS_ApplyLook(PSSettings& s, int look);         // colour/mood looks on top of a quality tier
float PS_NightFactor(int hours, int minutes);        // 0 = full day .. 1 = full night
void  PS_UpdateLive(PSSettings& s, float nightFactor);
