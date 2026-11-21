// TyFMCGFX 1.6.0 - public API for other AML mods
//
// Two ways to use it (both are the same functions):
//   1) AML interface:   IProperGFX* pgfx = (IProperGFX*)GetInterface("TyFMCGFX");    (#include <mod/interface.h>)
//   2) dlsym:           dlsym(dlopen("libTyFMCGFX64.so", RTLD_NOLOAD), "ProperGFX_SetValue") ...
//
// All setters are thread safe: changes are queued and applied on the render thread at the start of the next frame.
// Section / key names are exactly the ones in TyFMCGFX.ini (case-insensitive), e.g. ("Bloom", "Intensity", "0.6"),
// ("VolumetricFog", "Density.Night", "0.02"), ("General", "Quality", "Ultra"). Changes made through the API are NOT
// written to the ini unless you call Save().
#pragma once

#define PROPERGFX_API_VERSION 1

struct IProperGFX
{
    int         apiVersion;                                                         // PROPERGFX_API_VERSION
    const char* (*GetVersion)();                                                    // "1.5.0"
    bool        (*SetValue)(const char* section, const char* key, const char* value);   // false = unknown section/key
    bool        (*GetValue)(const char* section, const char* key, char* out, int outLen);
    bool        (*SetFloat)(const char* section, const char* key, float value);
    bool        (*SetBool)(const char* section, const char* key, bool value);
    void        (*SetEnabled)(bool enabled);                                        // master switch ([General] Enabled)
    bool        (*IsEnabled)();
    void        (*Reload)();                                                        // re-read the ini
    void        (*Save)();                                                          // write the current settings to the ini
    void        (*OpenMenu)();                                                      // open the in-game GUI editor
    void        (*SetLutFile)(const char* folder, const char* file);                // [LUT] Folder / File (file with .png)
};

#ifdef __cplusplus
extern "C" {
#endif
const char* ProperGFX_GetVersion();
bool ProperGFX_SetValue(const char* section, const char* key, const char* value);
bool ProperGFX_GetValue(const char* section, const char* key, char* out, int outLen);
bool ProperGFX_SetFloat(const char* section, const char* key, float value);
bool ProperGFX_SetBool(const char* section, const char* key, bool value);
void ProperGFX_SetEnabled(bool enabled);
bool ProperGFX_IsEnabled();
void ProperGFX_Reload();
void ProperGFX_Save();
void ProperGFX_OpenMenu();
void ProperGFX_SetLutFile(const char* folder, const char* file);
#ifdef __cplusplus
}
#endif
