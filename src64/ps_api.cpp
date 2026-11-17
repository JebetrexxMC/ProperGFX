// Proper GFX Mobile 1.5.0 - public API (see propergfx_api.h)
#include "ps_api.h"
#include "ps_ui.h"
#include <mutex>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <strings.h>

namespace {
struct Op { int kind; std::string a, b, c; };   // kind: 0 set value, 1 reload, 2 save
std::mutex       g_mx;
std::vector<Op>  g_ops;
PSSettings*      g_cfgp = nullptr;
std::string      g_ini;
PSSettings       g_shadow;   // copy readable from any thread (refreshed each Tick)
bool             g_have = false;

// validate a key without touching the live settings
bool KnownKey(const char* sec, const char* key) { PSSettings t; char tmp[8]; return PS_GetValue(t, sec, key, tmp, sizeof(tmp)); }
void Push(Op o) { std::lock_guard<std::mutex> l(g_mx); if (g_ops.size() < 4096) g_ops.push_back(std::move(o)); }
}

extern "C" {
__attribute__((visibility("default"))) const char* ProperGFX_GetVersion() { return PS_VERSION; }
__attribute__((visibility("default"))) bool ProperGFX_SetValue(const char* s, const char* k, const char* v)
{
    if (!s || !k || !v || !KnownKey(s, k)) return false;
    Push({0, s, k, v}); return true;
}
__attribute__((visibility("default"))) bool ProperGFX_GetValue(const char* s, const char* k, char* out, int n)
{
    std::lock_guard<std::mutex> l(g_mx);
    if (!g_have) return false;
    return PS_GetValue(g_shadow, s, k, out, n);
}
__attribute__((visibility("default"))) bool ProperGFX_SetFloat(const char* s, const char* k, float v) { char b[32]; snprintf(b, sizeof(b), "%.6f", v); return ProperGFX_SetValue(s, k, b); }
__attribute__((visibility("default"))) bool ProperGFX_SetBool(const char* s, const char* k, bool v) { return ProperGFX_SetValue(s, k, v ? "1" : "0"); }
__attribute__((visibility("default"))) void ProperGFX_SetEnabled(bool e) { ProperGFX_SetBool("General", "Enabled", e); }
__attribute__((visibility("default"))) bool ProperGFX_IsEnabled() { std::lock_guard<std::mutex> l(g_mx); return g_have && g_shadow.enabled; }
__attribute__((visibility("default"))) void ProperGFX_Reload() { Push({1, "", "", ""}); }
__attribute__((visibility("default"))) void ProperGFX_Save() { Push({2, "", "", ""}); }
__attribute__((visibility("default"))) void ProperGFX_OpenMenu() { UI_RequestOpen(); }
__attribute__((visibility("default"))) void ProperGFX_SetLutFile(const char* folder, const char* file)
{
    if (folder) ProperGFX_SetValue("LUT", "Folders", folder);
    if (file) ProperGFX_SetValue("LUT", "File", file);
}
}

static IProperGFX g_iface = {
    PROPERGFX_API_VERSION, ProperGFX_GetVersion, ProperGFX_SetValue, ProperGFX_GetValue, ProperGFX_SetFloat, ProperGFX_SetBool,
    ProperGFX_SetEnabled, ProperGFX_IsEnabled, ProperGFX_Reload, ProperGFX_Save, ProperGFX_OpenMenu, ProperGFX_SetLutFile,
};

void PSApi_Init(PSSettings* cfg, const char* /*dir*/, const char* iniPath)
{
    g_cfgp = cfg; g_ini = iniPath ? iniPath : "";
    std::lock_guard<std::mutex> l(g_mx); g_shadow = *cfg; g_have = true;
}
IProperGFX* PSApi_Get() { return &g_iface; }

void PSApi_Tick()
{
    if (!g_cfgp) return;
    std::vector<Op> ops;
    { std::lock_guard<std::mutex> l(g_mx); ops.swap(g_ops); }
    for (const Op& o : ops)
    {
        if (o.kind == 0) PS_SetValue(*g_cfgp, o.a.c_str(), o.b.c_str(), o.c.c_str());
        else if (o.kind == 1 && !g_ini.empty()) PS_LoadSettings(*g_cfgp, g_ini.c_str());
        else if (o.kind == 2 && !g_ini.empty()) PS_SaveSettings(*g_cfgp, g_ini.c_str());
    }
    std::lock_guard<std::mutex> l(g_mx); g_shadow = *g_cfgp;
}
