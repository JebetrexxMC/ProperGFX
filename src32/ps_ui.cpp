#include "ps_ui.h"
#include "ps_features.h"
#include "ps_guide.h"
#include "ps_log.h"
#include "ps_fx.h"
#include "ps_world.h"
#include "ps_lut.h"
#include <atomic>
#include <strings.h>
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include <GLES2/gl2.h>
#include <float.h>
#include <mutex>
#include <chrono>
#include <vector>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "ps_fsui.h"

namespace {

// ---------------------------------------------------------------- touch plumbing
struct TouchEv { int type, finger, x, y; bool primary; };
std::mutex g_mx;
std::vector<TouchEv> g_queue;
struct Rect { float x0, y0, x1, y1; bool valid; } g_rects[2] = {};   // [0] floating button, [1] menu window
bool g_captured[16] = {};
bool g_hiddenReset = false;            // 1.4.6: the UI was hidden (not in gameplay)
int  g_primary = -1;

bool InRects(float x, float y)
{
    for (const Rect& r : g_rects) if (r.valid && x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1) return true;
    return false;
}

// ---------------------------------------------------------------- ui state
ImGuiContext* g_ctx = nullptr;
bool  g_menuOpen = false;
bool  g_dirty = false;               // settings changed -> save when interaction ends
char  g_ini[512] = "";
float g_fontPx = 0.f;
int   g_screenW = 0, g_screenH = 0;
int   g_preset = 0;
int   g_testTab = -1;
// floating button interaction
bool  g_pressing = false, g_dragging = false;
float g_idle = 0.f;                       // seconds since the floating button was last touched
long long g_lastEvt = 0;                  // ms of the last captured touch event (watchdog)
long long NowMs() { return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
ImVec2 g_pressPos, g_grabOffset;

const char* kFontFiles[] = {
    "/system/fonts/Roboto-Regular.ttf", "/system/fonts/NotoSans-Regular.ttf", "/system/fonts/DroidSans.ttf",
    "/system/fonts/SamsungOne-400.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", nullptr };

// 1.6.0: the Firestorm "Preferences" look of Research/images (15).jpeg (green accents) - common/ps_fsui.h (shared with the 64-bit build)
void ApplyStyle(float sc) { fsui::ApplyStyle(sc); }

void BuildFonts(float px)
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    ImFont* f = nullptr;
    for (int i = 0; kFontFiles[i] && !f; ++i)
    {
        FILE* t = fopen(kFontFiles[i], "rb");
        if (!t) continue;
        fclose(t);
        f = io.Fonts->AddFontFromFileTTF(kFontFiles[i], px);
    }
    if (!f) { ImFontConfig fc; fc.SizePixels = px; f = io.Fonts->AddFontDefault(&fc); }
    io.Fonts->Build();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    g_fontPx = px;
}

// ---------------------------------------------------------------- widget helpers
int g_testMode = 0; float g_testScroll = 0.f;
bool g_worldTouched = false, g_userEdited = false, g_presetPressed = false, g_featDisabled = false;
#define TOUCHED(world) do { if (ImGui::IsItemEdited()) { g_userEdited = true; g_dirty = true; if (world) g_worldTouched = true; } } while (0)

// 1.6.0 "Search Settings": while the field has text only the matching rows are drawn (with the title of their feature)
char  g_search[48] = "";
bool  g_kbOpen = false;
bool  Searching() { return g_search[0] != 0; }
const char* g_featTitle = nullptr; bool* g_featEn = nullptr; bool g_featWorld = false, g_featShown = false, g_featAll = false;
void  EmitFeatureTitle()
{
    if (!g_featTitle || g_featShown) return;
    g_featShown = true;
    if (ImGui::Checkbox("##en", g_featEn)) { g_userEdited = true; g_dirty = true; if (g_featWorld) g_worldTouched = true; }
    ImGui::SameLine(); ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, fsui::GreenCheck()); ImGui::TextUnformatted(g_featTitle); ImGui::PopStyleColor();
    g_featDisabled = !*g_featEn;
    if (g_featDisabled) ImGui::BeginDisabled();
    ImGui::Indent();
}
bool  Vis(const char* label)               // false = skip this row (search filter)
{
    if (!Searching() || g_featAll) return true;
    if (!fsui::Match(label, g_search)) return false;
    EmitFeatureTitle();
    return true;
}
void Sep(const char* t) { if (!Searching()) ImGui::SeparatorText(t); }

bool RowSlider(const char* l, float* v, float a, float b, const char* fmt = "%.2f", bool world = false);
bool SliderF(const char* l, float* v, float a, float b, const char* fmt = "%.2f", bool world = false) { return RowSlider(l, v, a, b, fmt, world); }
// Day / Night pair (the PC mod keeps separate values for day and night, blended by the game clock)
void SliderDN(const char* l, DN* d, float a, float b, const char* fmt = "%.2f", bool world = false)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%s  (day)", l);   SliderF(buf, &d->day, a, b, fmt, world);
    snprintf(buf, sizeof(buf), "%s  (night)", l); SliderF(buf, &d->night, a, b, fmt, world);
}
bool Check(const char* l, bool* v, bool world = false)
{
    if (!Vis(l)) return false;
    bool ch = ImGui::Checkbox(l, v);
    TOUCHED(world);
    return ch;
}
// green drop-down (the Shadows / Water Reflections / Point Lighting boxes of the screenshot)
void PushGreenCombo()
{
    ImGui::PushStyleColor(ImGuiCol_FrameBg, fsui::GreenLo()); ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, fsui::Green());
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, fsui::GreenHi()); ImGui::PushStyleColor(ImGuiCol_Button, fsui::GreenLo());
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, fsui::Green());
}
void PopGreenCombo() { ImGui::PopStyleColor(5); }
bool Combo(const char* l, int* v, const char* const* items, int n, bool world = false)
{
    if (!Vis(l)) return false;
    ImGui::PushID(l);
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(l); ImGui::SameLine(avail * 0.46f);
    ImGui::SetNextItemWidth(avail * 0.50f);
    PushGreenCombo(); bool ch = ImGui::Combo("##c", v, items, n); PopGreenCombo();
    ImGui::PopID();
    if (ch) { g_userEdited = true; g_dirty = true; if (world) g_worldTouched = true; }
    return ch;
}

// One feature = one ini section: [Enabled] checkbox + collapsible header. Parameters are greyed out while disabled.
// Search mode: no header; the title is drawn (lazily) above the first matching row, or the whole feature when the title matches.
bool FeatureBegin(const char* title, bool* en, bool world = false)
{
    ImGui::PushID(title);
    if (Searching())
    {
        g_featTitle = title; g_featEn = en; g_featWorld = world; g_featShown = false; g_featDisabled = false;
        g_featAll = fsui::Match(title, g_search);
        if (g_featAll) EmitFeatureTitle();
        return true;
    }
    if (ImGui::Checkbox("##en", en)) { g_userEdited = true; g_dirty = true; if (world) g_worldTouched = true; }
    ImGui::SameLine();
    if (g_testMode) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    bool open = ImGui::CollapsingHeader(title);
    if (!open) { ImGui::PopID(); return false; }
    g_featDisabled = !*en;
    if (g_featDisabled) ImGui::BeginDisabled();
    ImGui::Indent();
    g_featShown = true;
    return true;
}
void FeatureEnd()
{
    if (g_featShown) ImGui::Unindent();
    if (g_featDisabled) ImGui::EndDisabled();
    g_featDisabled = false; g_featTitle = nullptr; g_featEn = nullptr; g_featShown = false; g_featAll = false;
    ImGui::PopID();
}
// "Label ........ ----o---- value" (label left, thin green track, value right - like the Firestorm preferences)
bool RowSlider(const char* l, float* v, float a, float b, const char* fmt, bool world)
{
    if (!Vis(l)) return false;
    ImGui::PushID(l);
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(l); ImGui::SameLine(avail * 0.46f);
    bool ch = fsui::Track("##v", v, a, b, avail * 0.36f);
    TOUCHED(world);
    ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::Text(fmt, *v);
    ImGui::PopID();
    return ch;
}
bool RowCombo(const char* l, int* v, const char* const* items, int n, bool world = false)
{
    if (!Vis(l)) return false;
    ImGui::PushID(l);
    ImGui::TextUnformatted(l);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.85f);
    PushGreenCombo(); bool ch = ImGui::Combo("##c", v, items, n); PopGreenCombo();
    if (ch) { g_userEdited = true; g_dirty = true; if (world) g_worldTouched = true; }
    ImGui::PopID();
    return ch;
}
void Note(const char* t) { if (Searching()) return; ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); ImGui::TextWrapped("%s", t); ImGui::PopStyleColor(); }
// action button inside a page (also filtered by the search)
bool Action(const char* l, const ImVec2& sz = ImVec2(0, 0)) { if (!Vis(l)) return false; return fsui::Button(l, sz); }

// 1.6.0: the old single list is split into Firestorm-style categories (left list); the search box looks through all feature pages
enum Page { P_GRAPHICS, P_LIGHTING, P_IMAGE, P_AA, P_CAMERA, P_ARTISTIC, P_INTERFACE, P_STATUS, P_GUIDE, P_COUNT };
const char* const kPageNames[P_COUNT] = { "Graphics", "Lighting", "Post Process", "Anti-aliasing", "Camera", "Artistic", "Interface", "Status", "Feature Guide" };
int g_page = 0, g_gfxTab = 0;
PSSettings g_snapshot; bool g_haveSnapshot = false;    // Cancel = back to the values from when the menu was opened
bool Page(int page, int n, const char* title)
{
    if (Searching()) { if (n == P_GRAPHICS || n == P_STATUS || n == P_GUIDE) return false; }
    else if (page != n) return false;
    Sep(title);
    return true;
}

void DrawFeatures(int page, PSSettings& s, const PSUIStatus& st)
{
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.58f);
    if (false) {
    // ---------------------------------------------------------------- presets (Low / Medium / High, like the PC mod)
    } if (Page(page, P_GRAPHICS, "PRESET")) {
    {
        float avail = ImGui::GetContentRegionAvail().x, sp = ImGui::GetStyle().ItemSpacing.x, bw = (avail - sp * 2.f) / 3.f;
        for (int i = 0; i < 3; ++i)
        {
            bool sel = s.quality == i;
            ImGui::PushStyleColor(ImGuiCol_Button, sel ? fsui::GreenHi() : fsui::SideIdle());   // Firestorm: selected = green, others = grey
            if (ImGui::Button(PS_QualityNames[i], ImVec2(bw, 0))) { PS_ApplyQuality(s, i); if (i >= 1) s.shadowRetry = true; g_presetPressed = true; g_dirty = true; g_worldTouched = true; }   // Medium / High also clear shadow safe mode
            ImGui::PopStyleColor();
            if (i < 2) ImGui::SameLine();
        }
        Note("Low = fast and smooth: no shadows, bloom or sun rays. Medium / High = shadows, bloom and sun rays.");
        if (s.quality == Q_CUSTOM) Note("Custom: you changed something after picking a preset.");
    }
    { int l = s.look; if (Combo("Look", &l, PS_LookNames, PS_LookCount)) { PS_ApplyLook(s, l); g_presetPressed = true; g_worldTouched = true; } }
    Combo("Time of day", &s.timeOfDay, PS_TimeNames, PS_TimeCount);
    Check("Adaptive quality", &s.adaptive);
    { static const char* fps[] = { "30 FPS", "60 FPS" }; Combo("Target FPS", &s.targetFps, fps, 2); Note("Set this to the game's FPS limit."); }
    if (st.fx && st.fx->adaptiveLevel > 0) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1), "Adaptive level %d active (heavy effects reduced)", st.fx->adaptiveLevel);

    // ---------------------------------------------------------------- lighting & atmosphere
    } if (Page(page, P_LIGHTING, "LIGHTING & ATMOSPHERE")) {
    if (FeatureBegin("SimulateHDR - HDR & tone mapping", &s.hdr))
    {
        Combo("Tonemapper", &s.tonemap, PS_TonemapNames, PS_TonemapCount);
        SliderDN("HDR amount", &s.hdrAmount, 0.f, 1.f);
        SliderDN("Exposure (EV)", &s.exposure, -3.f, 3.f);
        FeatureEnd();
    }
    if (FeatureBegin("AutoExposure - self-exposure", &s.autoExp))
    {
        SliderF("Bias (EV)", &s.autoBias, -2.f, 2.f);
        SliderF("Range (EV)", &s.autoRange, 0.f, 3.f);
        SliderF("Speed", &s.autoSpeed, 0.1f, 5.f);
        FeatureEnd();
    }
    if (FeatureBegin("HorizonFog - new horizon fog", &s.fogOn, true))
    {
        SliderDN("Density", &s.fogDensity, 0.f, 3.f, "%.2f", true);
        SliderF("Softness", &s.fogSoftness, -0.5f, 1.f, "%.2f", true);
        FeatureEnd();
    }
    if (FeatureBegin("WorldRainfall - wet surfaces", &s.wetWorld, true))
    {
        SliderF("Strength", &s.wetStrength, 0.f, 2.f, "%.2f", true);
        Note("Darkens the lit world while it rains (CWeather::WetRoads).");
        FeatureEnd();
    }
    const bool shPrev = s.shadows;
    const bool shOpen = FeatureBegin("DynamicShadows - sun shadows (game clock + timecyc.dat)", &s.shadows);
    if (!shPrev && s.shadows) s.shadowRetry = true;      // switching shadows on also clears safe mode
    if (shOpen)
    {
        SliderF("Strength", &s.shadowStrength, 0.f, 1.f);
        { static const char* res[] = { "512", "1024", "2048" }; Combo("Resolution", &s.shadowRes, res, 3); }
        SliderF("Distance (m)", &s.shadowDist, 25.f, 160.f, "%.0f");
        { int e = s.shadowEvery; if (ImGui::SliderInt("Update every N frames", &e, 1, 10, "%d", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp)) { s.shadowEvery = e; g_userEdited = true; g_dirty = true; } }
        SliderF("Softness", &s.shadowSoft, 0.3f, 3.f);
        SliderF("Bias", &s.shadowBias, 0.3f, 3.f);
        SliderF("Fade below sun elevation", &s.shadowMinSun, 0.f, 0.4f);
        SliderF("Sun smoothing", &s.sunSmoothing, 0.005f, 1.f, "%.3f");
        Check("Show shadow map (debug)", &s.shadowDebug);
        Combo("Sun source", &s.sunSource, PS_SunSourceNames, PS_SunSourceCount);
        Check("Strength follows timecyc.dat (hour / weather)", &s.shadowTimecyc);
        Check("Crash guard (safe mode after a failed launch)", &s.shadowGuard);
        Note("Sun source Auto = the game's time-cycle sun vector (follows the game clock). Strength can follow the Shdw column of timecyc.dat.");
        Note("Buildings, roads, terrain and vehicles cast and receive shadows; pedestrians do not.");
        if (!Searching() && s.shadows && st.shadowReceivers == 0 && st.patchedShaders >= 10)
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1), "Restart the game to finish turning shadows on (the receiver code is compiled into the shaders when the game starts).");
        if (st.shadow && !Searching())
        {
            const PSShadowStats& sh = *st.shadow;
            ImGui::Text("Casters: %d captured, %d rendered, %d skipped", sh.captured, sh.replayed, sh.rejected);
            ImGui::Text("Remembered: %d casters (%d kept although out of view), %d draws into other targets ignored", sh.cacheSize, sh.cacheKept, sh.otherTargets);
            ImGui::Text("Receivers: %d shader variants carry the shadow lookup", st.shadowReceivers);
            if (st.build)
            {
                const PSBuildStats& b = *st.build;
                ImGui::Text("Shader self-test: %d tested, %d ok, %d simplified, %d without shadow, %d original", b.tested, b.passed, b.simplified, b.noShadow, b.original);
                if (b.tested > b.passed) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1), "Your GPU driver rejected %d patched shaders (they fell back safely, no crash). Details: files/" PS_DIR_NAME "/tyfmcgfx_log.txt", b.tested - b.passed);
                if (b.shadowDisabled) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1), "Shadow receivers are off for this session: the driver rejects them. Send tyfmcgfx_log.txt.");
            }
            ImGui::Text("Map %d px   sun elevation %.2f   strength %.0f%%   %.1f ms", sh.resolution, sh.sunElevation, sh.fade * 100.f, sh.cpuMs);
            if (sh.timecycValid) ImGui::Text("timecyc.dat Shdw %d / %d  ->  strength x%.2f", sh.timecycRaw, sh.timecycMax, sh.timecycMul); else ImGui::TextDisabled("timecyc.dat strength: not available");
            ImGui::Text("Sun (%.2f %.2f %.2f) from %s", sh.sunDir[0], sh.sunDir[1], sh.sunDir[2], sh.sunSource == 1 ? "time cycle" : (sh.sunSource == 2 ? "light uniform" : "-"));
            ImGui::TextDisabled("skipped: blend %d, no depth write %d, no index buf %d, vertex fmt %d, shader %d, mode %d, far %d, stale %d",
                                sh.rejBlend, sh.rejNoDepthWrite, sh.rejNoIndexBuffer, sh.rejVertexFormat, sh.rejNoShaderInfo, sh.rejMode, sh.rejDistance, sh.rejStale);
            ImGui::TextWrapped("Status: %s", sh.note[0] ? sh.note : (sh.ready ? "ok" : "waiting for the first frame"));
            if (sh.guardTripped || (sh.note[0] && strstr(sh.note, "SAFE MODE")))
                if (Action("Retry shadows (picking Medium / High does this too)")) s.shadowRetry = true;
        }
        FeatureEnd();
    }
    if (FeatureBegin("SurfaceShading - specular & reflections", &s.surfaceOn, true))
    {
        SliderF("Specular", &s.specularMul, 0.f, 3.f, "%.2f", true);
        SliderF("Vehicle reflections", &s.reflectionMul, 0.f, 3.f, "%.2f", true);
        FeatureEnd();
    }
    if (FeatureBegin("WaterSurface", &s.waterOn, true))
    {
        SliderF("Detail", &s.waterDetail, 0.f, 3.f, "%.2f", true);
        FeatureEnd();
    }
    if (FeatureBegin("Underwater", &s.underwater))
    {
        SliderF("Strength", &s.underwaterStrength, 0.f, 1.f);
        Note("Distortion, tint and caustic shimmer.");
        FeatureEnd();
    }

    // ---------------------------------------------------------------- image
    } if (Page(page, P_IMAGE, "IMAGE")) {
    if (FeatureBegin("Bloom", &s.bloom))
    {
        SliderDN("Intensity", &s.bloomIntensity, 0.f, 1.5f);
        SliderF("Threshold", &s.bloomThreshold, 0.2f, 1.f);
        SliderF("Radius", &s.bloomRadius, 0.3f, 3.f);
        FeatureEnd();
    }
    if (FeatureBegin("SunRays", &s.sunRays))
    {
        SliderF("Intensity", &s.sunRaysIntensity, 0.f, 1.5f);
        SliderF("Decay", &s.sunRaysDecay, 0.80f, 0.99f);
        if (!st.sunTracked) Note("Sun position not available yet (camera not found).");
        FeatureEnd();
    }
    if (FeatureBegin("LensFlare", &s.lensFlare))
    {
        SliderF("Intensity", &s.lensFlareIntensity, 0.f, 1.5f);
        FeatureEnd();
    }
    if (FeatureBegin("ColorGrading - grade, LUT", &s.grading))
    {
        Combo("Grade", &s.gradePreset, PS_GradeNames, PS_GradeCount);
        SliderF("Grade amount", &s.gradeAmount, 0.f, 1.f);
        SliderDN("Contrast", &s.contrast, 0.5f, 1.6f);
        SliderDN("Saturation", &s.saturation, 0.f, 2.f);
        SliderF("Vibrance", &s.vibrance, -1.f, 1.f);
        SliderF("Temperature", &s.temperature, -1.f, 1.f);
        SliderF("Tint", &s.tint, -1.f, 1.f);
        SliderF("Gamma", &s.gamma, 0.6f, 1.6f);
        SliderF("Lift", &s.lift, -0.2f, 0.2f);
        SliderF("Gain", &s.gain, 0.6f, 1.5f);
        FeatureEnd();
    }
    if (FeatureBegin("LUT - colour lookup texture", &s.lutOn))
    {
        SliderF("Amount", &s.lutAmount, 0.f, 1.f);
        {   // file picker: every image in files/TyFMCGFX and files/TyFMCGFX/luts
            static std::vector<std::string> files; static float scan = 99.f; scan += ImGui::GetIO().DeltaTime;
            if (scan > 2.f && st.dir && st.dir[0]) { LUT_ListFiles(st.dir, files); scan = 0.f; }
            const bool isAuto = !strcasecmp(s.lutFile, "auto") || !strcasecmp(s.lutFile, "lut") || !s.lutFile[0];
            if (Vis("File") && ImGui::BeginCombo("File", isAuto ? "auto (lut.png / .bmp / .tga / .jpg)" : s.lutFile))
            {
                if (ImGui::Selectable("auto (lut.png / .bmp / .tga / .jpg)", isAuto)) { snprintf(s.lutFile, sizeof(s.lutFile), "auto"); g_dirty = true; g_userEdited = true; }
                for (const std::string& f : files)
                    if (ImGui::Selectable(f.c_str(), !strcasecmp(f.c_str(), s.lutFile))) { snprintf(s.lutFile, sizeof(s.lutFile), "%s", f.c_str()); g_dirty = true; g_userEdited = true; }
                ImGui::EndCombo();
            }
        }
        if (Action("Reload LUT")) LUT_RequestReload();
        const PSLutInfo& li = LUT_Info();
        if (!Searching()) { if (li.loaded) ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.5f, 1), "Loaded: %s  %dx%d  (%d^3%s)", li.file, li.w, li.h, li.n, li.flipped ? ", green bottom-up -> turned" : "");
        else ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1), "Not loaded: %s - %s", li.file[0] ? li.file : "lut", li.error[0] ? li.error : "no file"); }
        Note("Put lut.png / lut.bmp / lut.tga / lut.jpg (or any image picked above) in files/" PS_DIR_NAME " or files/" PS_DIR_NAME "/luts. Sizes: 256x16, 1024x32, 4096x64 strips, 64x64 or 512x512 squares. Changes are picked up live.");
        FeatureEnd();
    }
    if (FeatureBegin("Dithering - banding fix", &s.dither)) { Note("Triangular dither in the final pass (dark gradients, sky)."); FeatureEnd(); }
    if (FeatureBegin("Vignette", &s.vignette))
    {
        SliderDN("Strength", &s.vignetteStrength, 0.f, 1.f);
        SliderF("Size", &s.vignetteSize, 0.2f, 1.2f);
        FeatureEnd();
    }

    // ---------------------------------------------------------------- anti-aliasing & sharpen
    } if (Page(page, P_AA, "ANTI-ALIASING & SHARPEN")) {
    if (FeatureBegin("AntiAliasing - FXAA (SMAA is not possible)", &s.fxaa)) { SliderF("Strength", &s.fxaaStrength, 0.f, 1.f); FeatureEnd(); }
    if (FeatureBegin("Sharpen - AMD CAS", &s.cas)) { SliderF("Strength", &s.casStrength, 0.f, 1.f); FeatureEnd(); }

    // ---------------------------------------------------------------- camera
    } if (Page(page, P_CAMERA, "CAMERA")) {
    if (FeatureBegin("ImprovedZFighting - near clip", &s.zfix))
    {
        SliderF("Near clip", &s.nearClip, 0.05f, 2.f);
        Note("Raise to reduce z-fighting; lower to avoid first-person clipping.");
        FeatureEnd();
    }
    if (FeatureBegin("DepthOfField - tilt-shift (no depth buffer)", &s.tiltShift))
    {
        SliderF("Focus position", &s.tiltFocus, 0.f, 1.f);
        SliderF("Focus width", &s.tiltWidth, 0.05f, 0.6f);
        SliderF("Strength", &s.tiltStrength, 0.f, 1.f);
        FeatureEnd();
    }

    // ---------------------------------------------------------------- artistic
    } if (Page(page, P_ARTISTIC, "ARTISTIC")) {
    if (FeatureBegin("CelShading - anime", &s.cel))
    {
        SliderF("Levels", &s.celLevels, 2.f, 12.f, "%.0f");
        SliderF("Amount", &s.celAmount, 0.f, 1.f);
        Check("Also cel-shade the world (restart)", &s.celWorld);
        SliderF("World levels", &s.celWorldLevels, 2.f, 12.f, "%.0f", true);
        FeatureEnd();
    }
    if (FeatureBegin("Outlines - GTA:CW style", &s.outline))
    {
        SliderF("Strength", &s.outlineStrength, 0.f, 1.f);
        SliderF("Threshold", &s.outlineThreshold, 0.05f, 0.8f);
        FeatureEnd();
    }
    if (FeatureBegin("VHS", &s.vhs)) { SliderF("Strength", &s.vhsStrength, 0.f, 1.f); FeatureEnd(); }
    if (FeatureBegin("FilmGrain", &s.grain)) { SliderF("Strength", &s.grainStrength, 0.f, 1.f); FeatureEnd(); }
    if (FeatureBegin("ChromaticAberration", &s.chromatic)) { SliderF("Strength", &s.chromaticStrength, 0.f, 1.f); FeatureEnd(); }

    // ---------------------------------------------------------------- feature guide
    } if (Page(page, P_GUIDE, "FEATURE GUIDE")) {
    if (ImGui::CollapsingHeader("What each effect does (and what this build really has)"))
    {
        for (int i = 0; i < kGuideCount; ++i)
        {
            const PSGuide& g = kGuide[i];
            ImVec4 col = g.status == 0 ? ImVec4(0.45f, 0.95f, 0.5f, 1) : (g.status == 1 ? ImVec4(1.0f, 0.82f, 0.3f, 1) : ImVec4(1.0f, 0.42f, 0.4f, 1));
            ImGui::PushID(i);
            ImGui::TextColored(col, "%s", g.status == 0 ? "[OK]" : (g.status == 1 ? "[~]" : "[X]")); ImGui::SameLine();
            ImGui::TextWrapped("%s", g.name);
            ImGui::Indent();
            ImGui::TextWrapped("%s", g.what);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("In this build: %s", g.here);
            ImGui::PopStyleColor();
            ImGui::Unindent();
            ImGui::PopID();
            ImGui::Spacing();
        }
    }

    // ---------------------------------------------------------------- not possible on mobile
    } if (Page(page, P_GUIDE, "NOT AVAILABLE ON MOBILE")) {
    if (ImGui::CollapsingHeader("TyFMCGFX feature list (what this build has)"))
    {
        ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.5f, 1), "[OK]"); ImGui::SameLine(); ImGui::TextUnformatted("implemented");
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.3f, 1), "[~]");  ImGui::SameLine(); ImGui::TextUnformatted("approximated");
        ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.4f, 1), "[X]");  ImGui::SameLine(); ImGui::TextUnformatted("engine = cannot do in GTA SA Mobile");
        ImGui::Spacing();
        for (int i = 0; i < kFeatureCount; ++i)
        {
            const PSFeature& f = kFeatures[i];
            ImVec4 col = f.status == 0 ? ImVec4(0.45f, 0.95f, 0.5f, 1) : (f.status == 1 ? ImVec4(1.0f, 0.82f, 0.3f, 1) : ImVec4(1.0f, 0.42f, 0.4f, 1));
            ImGui::TextColored(col, "%s", f.status == 0 ? "[OK]" : (f.status == 1 ? "[~]" : "[X]")); ImGui::SameLine();
            ImGui::TextWrapped("%s", f.name);
            if (f.status == 2) ImGui::TextDisabled("    engine = cannot do in GTA SA Mobile");
        }
    }

    // ---------------------------------------------------------------- status & menu
    } if (Page(page, P_STATUS, "STATUS")) {
    if (ImGui::CollapsingHeader("Renderer & performance"))
    {
        ImGui::TextWrapped("Renderer: %s", st.fxStatus);
        ImGui::TextWrapped("Game: %s", st.gameInfo);
        ImGui::Text("World shaders patched: %d", st.patchedShaders);
        ImGui::Text("Sun tracking: %s", st.sunTracked ? "yes" : "no");
        if (st.fx)
        {
            ImGui::Text("Frame time (avg): %.1f ms  (%.0f FPS)", st.fx->avgFrameMs, st.fx->avgFrameMs > 0.1f ? 1000.f / st.fx->avgFrameMs : 0.f);
            ImGui::Text("Adaptive level: %d    GL passes: %d", st.fx->adaptiveLevel, st.fx->passes);
            ImGui::Text("Shader variants cached: %d", st.fx->variants);
            ImGui::Text("Full-res texture fetches/pixel: ~%.0f", st.fx->fullResFetches);
        }
    }
    } if (Page(page, P_INTERFACE, "MENU & FLOATING BUTTON")) {
    {
        Check("Show floating button", &s.showButton);
        Check("Lock the button (no dragging)", &s.buttonLock);
        Check("Shrink to a dot when idle", &s.buttonCollapse);
        float px = s.buttonX * 100.f, py = s.buttonY * 100.f;
        if (RowSlider("X position", &px, 0.f, 100.f, "%.1f%%")) s.buttonX = px / 100.f;
        if (RowSlider("Y position", &py, 0.f, 100.f, "%.1f%%")) s.buttonY = py / 100.f;
        SliderF("Button size", &s.buttonScale, 0.5f, 2.5f, "%.2fx");
        SliderF("Button opacity", &s.buttonOpacity, PS_BUTTON_MIN_OPACITY, 1.f);
        SliderF("Menu scale", &s.uiScale, 0.6f, 2.0f, "%.2fx");
        Check("World shader patches (restart)", &s.worldPatch);
        Note("Drag the floating button anywhere; tap it to open this menu.");
    }
    }
    ImGui::PopItemWidth();
}

// 1.5.0 TyFMCGFX symbol, vector only (no texture, no text): a gear ring with 8 teeth around a half-filled circle
// (the "light / shadow" half disc = shader settings). Drawn with ImDrawList so it stays sharp at every size.
void DrawModIcon(ImDrawList* dl, ImVec2 c, float r, ImU32 col, ImU32 cut)
{
    const int teeth = 8; const float PI = 3.14159265f;
    const float rIn = r * 0.80f, rOut = r;
    // gear: solid disc + 8 convex teeth (no triangle fan -> no anti-aliasing seams)
    dl->AddCircleFilled(c, rIn, col, 40);
    for (int i = 0; i < teeth; ++i)
    {
        float a0 = (i / (float)teeth) * 2.f * PI, w = 2.f * PI / teeth;
        float t[4] = { a0 - w * 0.24f, a0 - w * 0.15f, a0 + w * 0.15f, a0 + w * 0.24f };
        float rr[4] = { rIn * 0.96f, rOut, rOut, rIn * 0.96f };
        ImVec2 q[4];
        for (int k = 0; k < 4; ++k) q[k] = ImVec2(c.x + cosf(t[k]) * rr[k], c.y + sinf(t[k]) * rr[k]);
        dl->AddConvexPolyFilled(q, 4, col);
    }
    // hollow the ring
    dl->AddCircleFilled(c, r * 0.60f, cut, 32);
    // half-filled disc (left half solid, right half outlined): the lit / shaded symbol
    const float rd = r * 0.40f;
    dl->PathArcTo(c, rd, PI * 0.5f, PI * 1.5f, 20); dl->PathFillConvex(col);
    dl->AddCircle(c, rd, col, 28, fmaxf(1.2f, r * 0.08f));
}

// 1.6.0 floating button = Firestorm status-bar pill (Research/images (15).jpeg, red arrow): device clock, the mod icon, the frame rate,
// red end cap. Idle + "shrink" = the icon only. Drawn on the foreground list, hit-tested by hand (raw touch input).
float g_fpsShown = 0.f, g_fpsTimer = 0.f;
void DrawButton(PSSettings& s, int W, int H, const PSUIStatus& st)
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    g_idle += io.DeltaTime;
    bool collapsed = s.buttonCollapse && !g_menuOpen && !g_pressing && g_idle > 4.f;
    char clock[24], fps[16];
    {
        time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
        int h12 = lt.tm_hour % 12; if (h12 == 0) h12 = 12;
        snprintf(clock, sizeof(clock), "%d:%02d:%02d %s", h12, lt.tm_min, lt.tm_sec, lt.tm_hour < 12 ? "AM" : "PM");
        g_fpsTimer -= io.DeltaTime;
        if (g_fpsTimer <= 0.f)
        {
            float ms = (st.fx && st.fx->avgFrameMs > 0.1f) ? st.fx->avgFrameMs : 0.f;
            g_fpsShown = ms > 0.f ? 1000.f / ms : io.Framerate; g_fpsTimer = 0.5f;
        }
        snprintf(fps, sizeof(fps), "%.1f", g_fpsShown);
    }
    const float side = g_fontPx * 1.9f * s.buttonScale * (collapsed ? 0.85f : 1.0f);
    fsui::Pill pm = fsui::MeasurePill(side, clock, fps, collapsed);
    ImVec2 sz(pm.mx.x, pm.mx.y);
    ImVec2 c(s.buttonX * W, s.buttonY * H);
    ImVec2 mn(c.x - sz.x * 0.5f, c.y - sz.y * 0.5f);
    if (mn.x < 0) mn.x = 0; if (mn.y < 0) mn.y = 0;
    if (mn.x + sz.x > W) mn.x = W - sz.x; if (mn.y + sz.y > H) mn.y = H - sz.y;
    ImVec2 mx(mn.x + sz.x, mn.y + sz.y);

    bool overMenu = false;
    if (g_menuOpen && g_rects[1].valid)
    {
        std::lock_guard<std::mutex> l(g_mx);
        overMenu = io.MousePos.x >= g_rects[1].x0 && io.MousePos.x <= g_rects[1].x1 && io.MousePos.y >= g_rects[1].y0 && io.MousePos.y <= g_rects[1].y1;
    }
    bool hover = io.MousePos.x >= mn.x && io.MousePos.x <= mx.x && io.MousePos.y >= mn.y && io.MousePos.y <= mx.y && !overMenu;
    if (io.MouseClicked[0] && hover) { g_pressing = true; g_dragging = false; g_idle = 0.f; g_pressPos = io.MousePos; g_grabOffset = ImVec2(c.x - io.MousePos.x, c.y - io.MousePos.y); }
    if (g_pressing && io.MouseDown[0])
    {
        float dx = io.MousePos.x - g_pressPos.x, dy = io.MousePos.y - g_pressPos.y;
        g_idle = 0.f;
        if (!s.buttonLock && !g_dragging && sqrtf(dx * dx + dy * dy) > g_fontPx * 0.45f) g_dragging = true;   // [Button] Lock
        if (g_dragging)
        {
            s.buttonX = (io.MousePos.x + g_grabOffset.x) / W; s.buttonY = (io.MousePos.y + g_grabOffset.y) / H;
            if (s.buttonX < 0.f) s.buttonX = 0.f; if (s.buttonX > 1.f) s.buttonX = 1.f;
            if (s.buttonY < 0.f) s.buttonY = 0.f; if (s.buttonY > 1.f) s.buttonY = 1.f;
        }
    }
    if (g_pressing && io.MouseReleased[0])
    {
        if (!g_dragging) { g_menuOpen = !g_menuOpen; if (!g_menuOpen) g_dirty = true; }
        else g_dirty = true;
        g_pressing = false; g_dragging = false; g_idle = 0.f;
    }
    float a = s.buttonOpacity;   // 0 = nothing drawn at all (the button still works)
    bool active = g_pressing || g_menuOpen;
    if (a > 0.001f)
    {
        const float a2 = fminf(1.f, a + 0.25f);
        ImVec2 ic = fsui::DrawPill(dl, mn, mx, clock, fps, collapsed, active, a2);
        ImU32 bg = fsui::U(active ? fsui::C(0.10f, 0.20f, 0.09f) : fsui::C(0.f, 0.f, 0.f), a2 * (collapsed ? 0.70f : 0.88f));
        DrawModIcon(dl, ic, side * 0.34f, fsui::U(active ? fsui::GreenCheck() : fsui::C(1, 1, 1), a2), bg);
    }
    std::lock_guard<std::mutex> l(g_mx);
    g_rects[0] = { mn.x - 3.f, mn.y - 3.f, mx.x + 3.f, mx.y + 3.f, true };
}

} // namespace

bool UI_TestButtonRect(float out[4]) { std::lock_guard<std::mutex> l(g_mx); if (!g_rects[0].valid) return false; out[0] = g_rects[0].x0; out[1] = g_rects[0].y0; out[2] = g_rects[0].x1; out[3] = g_rects[0].y1; return true; }
void UI_TestOpen(bool o, int tab) { g_menuOpen = o; g_page = (tab >= 0 && tab < P_COUNT) ? tab : 0; g_testMode = tab >= 1 ? 1 : 0; g_testScroll = 0.f; }
void UI_SetIniPath(const char* p) { snprintf(g_ini, sizeof(g_ini), "%s", p ? p : ""); }
bool UI_MenuOpen() { return g_menuOpen; }
static std::atomic<bool> g_openRequest{false};
void UI_RequestOpen() { g_openRequest = true; }

bool UI_Init()
{
    if (g_ctx) return true;
    IMGUI_CHECKVERSION();
    g_ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.MouseDrawCursor = false;
    if (!ImGui_ImplOpenGL3_Init("#version 100")) { PS_Log("ImGui GL backend init failed"); return false; }
    return true;
}

void UI_ContextLost()
{
    if (!g_ctx) return;
    ImGui_ImplOpenGL3_Shutdown();       // backend only forgets names; GL objects died with the context
    ImGui::DestroyContext(g_ctx);
    g_ctx = nullptr; g_fontPx = 0.f;
}

void UI_Shutdown()
{
    if (!g_ctx) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext(g_ctx);
    g_ctx = nullptr;
}

bool UI_OnTouch(int type, int finger, int x, int y)
{
    if (finger < 0 || finger >= 16) return false;
    std::lock_guard<std::mutex> l(g_mx);
    if (type == 2)      // push: ALWAYS starts a fresh sequence for this finger (a stale capture must never swallow a game touch)
    {
        if (g_captured[finger] && finger == g_primary) g_primary = -1;
        g_captured[finger] = false;
        if (!InRects((float)x, (float)y)) return false;
        g_captured[finger] = true;
        bool primary = (g_primary < 0);
        if (primary) g_primary = finger;
        g_queue.push_back({ type, finger, x, y, primary }); g_lastEvt = NowMs();
        return true;
    }
    if (!g_captured[finger]) return false;
    bool primary = (finger == g_primary);
    if (type == 4) type = 1;            // cancel == release
    g_queue.push_back({ type, finger, x, y, primary }); g_lastEvt = NowMs();
    if (type == 1) { g_captured[finger] = false; if (primary) g_primary = -1; }
    return true;
}

void UI_Render(PSSettings& s, int W, int H, float dt, const PSUIStatus& st, bool gameplay)
{
    if (!gameplay)
    {   // 1.4.6: main menu / pause menu / loading screen: no button, no window, and no touch is taken from the game.
        // A pending "open menu" request (Settings > Mods) waits until gameplay resumes.
        std::lock_guard<std::mutex> l(g_mx);
        g_rects[0].valid = false; g_rects[1].valid = false;
        for (int i = 0; i < 16; ++i) g_captured[i] = false;
        g_primary = -1; g_queue.clear(); g_hiddenReset = true;
        return;
    }
    if (!g_ctx && !UI_Init()) return;
    if (g_hiddenReset)
    {   // back in gameplay: a press that was cut off by the menu must not stay "down" inside ImGui
        g_hiddenReset = false; g_pressing = false; g_dragging = false;
        ImGuiIO& io0 = ImGui::GetIO(); io0.AddMouseButtonEvent(0, false); io0.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
    if (g_openRequest.exchange(false)) g_menuOpen = true;
    if (!s.showButton && !g_menuOpen) { std::lock_guard<std::mutex> l(g_mx); g_rects[0].valid = false; g_rects[1].valid = false; return; }

    ImGuiIO& io = ImGui::GetIO();
    g_screenW = W; g_screenH = H;
    float px = floorf(fmaxf(16.f, fminf(56.f, H * 0.03f * s.uiScale)));
    if (fabsf(px - g_fontPx) > 0.5f) { BuildFonts(px); ApplyStyle(px / 18.f); }
    io.DisplaySize = ImVec2((float)W, (float)H);
    io.DeltaTime = dt > 0.0001f ? dt : 0.016f;

    std::vector<TouchEv> evs;
    bool watchdog = false;
    {
        std::lock_guard<std::mutex> l(g_mx);
        evs.swap(g_queue);
        // watchdog: a captured finger that has been silent for 2.5 s lost its RELEASE (pointer ids can shift on multi-touch) -> let go,
        // otherwise every later event of that finger id would be swallowed and other mods' buttons would seem dead
        if (g_primary >= 0 && evs.empty() && NowMs() - g_lastEvt > 2500) { g_captured[g_primary] = false; g_primary = -1; watchdog = true; }
    }
    if (watchdog) { g_pressing = false; g_dragging = false; io.AddMouseButtonEvent(0, false); io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); }
    for (const TouchEv& e : evs)
    {
        if (!e.primary) continue;
        io.AddMousePosEvent((float)e.x, (float)e.y);
        if (e.type == 2) io.AddMouseButtonEvent(0, true);
        else if (e.type == 1) { io.AddMouseButtonEvent(0, false); io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    g_worldTouched = false; g_userEdited = false; g_presetPressed = false;

    if (g_menuOpen && !g_haveSnapshot) { g_snapshot = s; g_haveSnapshot = true; }
    if (g_menuOpen)
    {
        // centred, ends above the bottom edge so the default button position (bottom centre) stays reachable to close the menu
        float ww = fminf(W * 0.94f, px * 52.f), wh = fminf(H * 0.86f, px * 34.f);
        ImGui::SetNextWindowPos(ImVec2((W - ww) * 0.5f, fmaxf(H * 0.02f, (H * 0.88f - wh) * 0.5f)), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(ww, wh), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(px * 24.f, px * 14.f), ImVec2((float)W, (float)H));
        bool open = true, cancel = false;
        if (ImGui::Begin("Preferences###pgfx", &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar))
        {
            const ImGuiStyle& sty = ImGui::GetStyle();
            // ---- title row: "Preferences"  ...  ?  _  x   (drag the row to move the window: ImGui moves windows by any empty area)
            {
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Preferences");
                ImGui::SameLine(); ImGui::TextDisabled("  TyFMCGFX " PS_VERSION);
                const float gb = ImGui::GetFrameHeight() * 0.95f, gs = 2.f;
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - gb * 3.f - gs * 2.f);
                if (fsui::GlyphButton("?")) { g_page = P_GUIDE; g_search[0] = 0; }
                ImGui::SameLine(0, gs); if (fsui::GlyphButton("_")) open = false;      // minimise = close, keep the changes
                ImGui::SameLine(0, gs); if (fsui::GlyphButton("x")) open = false;
            }
            // ---- search row
            {
                bool kb = g_kbOpen;
                fsui::SearchField(g_search, &kb, ImGui::GetContentRegionAvail().x);
                g_kbOpen = kb;
                if (g_kbOpen) fsui::Keyboard(g_search, sizeof(g_search), &g_kbOpen);
            }
            const float footer = ImGui::GetFrameHeightWithSpacing() + sty.ItemSpacing.y;
            // ---- left: category list (selected = green)
            float sideW = 0.f;
            for (int i = 0; i < P_COUNT; ++i) sideW = fmaxf(sideW, ImGui::CalcTextSize(kPageNames[i]).x);
            sideW += sty.FramePadding.x * 2.f + sty.WindowPadding.x * 2.f + sty.ScrollbarSize + sty.ItemSpacing.x;
            ImGui::BeginChild("##side", ImVec2(sideW, -footer), true);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(sty.ItemSpacing.x, 1.f));
            for (int i = 0; i < P_COUNT; ++i)
                if (fsui::SideButton(kPageNames[i], g_page == i && !Searching(), -1.f)) { g_page = i; g_search[0] = 0; g_kbOpen = false; }
            ImGui::PopStyleVar();
            if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseDragging(0, 8.f)) ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginGroup();
            int page = g_page;
            if (page == P_GRAPHICS && !Searching())
            {   // Firestorm tab row
                static const char* tabs[4] = { "General", "Rendering", "Post Process", "Depth of Field" };
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.f, sty.ItemSpacing.y));
                for (int i = 0; i < 4; ++i) { if (fsui::TabButton(tabs[i], g_gfxTab == i)) g_gfxTab = i; if (i < 3) ImGui::SameLine(); }
                ImGui::PopStyleVar();
            }
            ImGui::BeginChild("##scroll", ImVec2(0, -footer), true);
            static const int kTabPage[4] = { P_GRAPHICS, P_LIGHTING, P_IMAGE, P_CAMERA };
            if (Searching()) { ImGui::PushID("search"); DrawFeatures(-1, s, st); ImGui::PopID(); }
            else DrawFeatures(page == P_GRAPHICS ? kTabPage[g_gfxTab] : page, s, st);
            if (g_testScroll > 0.f) { ImGui::SetScrollY(g_testScroll); }
            if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseDragging(0, 8.f))
                ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
            ImGui::EndChild();
            ImGui::EndGroup();
            // ---- footer: Reset left, OK / Cancel right
            if (fsui::Button("Reset to defaults")) { PS_ResetSettings(s); g_worldTouched = true; g_dirty = true; }
            {
                float bw = px * 5.5f, sp = sty.ItemSpacing.x;
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - bw * 2.f - sp);
                if (fsui::Button("OK", ImVec2(bw, 0))) open = false;
                ImGui::SameLine();
                if (fsui::Button("Cancel", ImVec2(bw, 0))) { cancel = true; open = false; }
            }
            ImVec2 p = ImGui::GetWindowPos(), z = ImGui::GetWindowSize();
            std::lock_guard<std::mutex> l(g_mx);
            g_rects[1] = { p.x, p.y, p.x + z.x, p.y + z.y, true };
        }
        else { std::lock_guard<std::mutex> l(g_mx); g_rects[1].valid = false; }
        ImGui::End();
        if (!open)
        {
            if (cancel && g_haveSnapshot) { s = g_snapshot; g_worldTouched = true; g_presetPressed = true; }
            g_menuOpen = false; g_haveSnapshot = false;
            if (g_ini[0]) PS_SaveSettings(s, g_ini); g_dirty = false;
        }
    }
    else { g_haveSnapshot = false; }
    if (!g_menuOpen) { std::lock_guard<std::mutex> l(g_mx); g_rects[1].valid = false; }

    if (s.shadowDebug && st.shadowTex)
    {   // shadow map preview (packed depth colours) so problems can be seen on screenshots
        ImDrawList* dl = ImGui::GetForegroundDrawList(); float sz = H * 0.26f; ImVec2 p0(W - sz - 8.f, 8.f), p1(W - 8.f, 8.f + sz);
        dl->AddRectFilled(ImVec2(p0.x - 3, p0.y - 3), ImVec2(p1.x + 3, p1.y + 3), IM_COL32(0, 0, 0, 200));
        dl->AddImage((ImTextureID)(intptr_t)st.shadowTex, p0, p1, ImVec2(0, 1), ImVec2(1, 0));
        dl->AddText(ImVec2(p0.x + 4, p0.y + 2), IM_COL32(255, 220, 120, 255), "shadow map");
    }
    if (s.showButton) DrawButton(s, W, H, st);
    else { std::lock_guard<std::mutex> l(g_mx); g_rects[0].valid = false; }

    if (g_userEdited && !g_presetPressed && s.quality != Q_CUSTOM) s.quality = Q_CUSTOM;
    if (g_worldTouched) ++g_worldGen;
    // autosave once the finger is lifted
    if (g_dirty && !io.MouseDown[0] && g_ini[0]) { PS_SaveSettings(s, g_ini); g_dirty = false; }

    ImGui::Render();
    glViewport(0, 0, W, H);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
