# Proper GFX Mobile public API (64-bit, 1.5.0)

Header: [`src64/propergfx_api.h`](src64/propergfx_api.h). Copy it into your mod; nothing else is needed.

## Getting the API
```cpp
#include <mod/amlmod.h>
#include <mod/interface.h>
#include "propergfx_api.h"

IProperGFX* pgfx = nullptr;

ON_MOD_LOAD()   // or OnAllModsLoaded
{
    pgfx = (IProperGFX*)GetInterface("ProperGFX");     // registered by libProperGFX64.so at load
    if (pgfx && pgfx->apiVersion >= PROPERGFX_API_VERSION)
    {
        pgfx->SetValue("Bloom", "Intensity", "0.6");
        pgfx->SetBool("VolumetricFog", "Enabled", true);
        pgfx->SetFloat("VolumetricFog", "Density.Night", 0.5f);
    }
}
```
Without AML interfaces you can use the exported C functions: `dlsym(dlopen("libProperGFX64.so", RTLD_NOLOAD), "ProperGFX_SetValue")`.

## Functions
| Function | What it does |
|---|---|
| `GetVersion()` | `"1.5.0"` |
| `SetValue(section, key, value)` | Set any ini value as text. Returns false for an unknown section / key. |
| `GetValue(section, key, out, len)` | Read a value as text (the value currently applied, plus changes still queued). |
| `SetFloat` / `SetBool` | Typed helpers for `SetValue`. |
| `SetEnabled(bool)` / `IsEnabled()` | Master switch (`[General] Enabled`). |
| `Reload()` | Re-read `TYFMC.ProperGFX.ini`. |
| `Save()` | Write the current settings to the ini. API changes are not saved unless you call this. |
| `OpenMenu()` | Open the in-game settings menu. |
| `SetLutFile(folder, file)` | `[LUT] Folders` + `File` (for example `("luts", "warm.png")`). |

## Rules
* Section / key names are the ones in `TYFMC.ProperGFX.ini` (case-insensitive). Day / night values use `Key.Day` / `Key.Night`.
* All calls are thread safe. Changes are queued and applied on the render thread at the start of the next frame.
* Combo values accept their names (`("AmbientOcclusion", "Mode", "SSGI")`, `("General", "Quality", "Ultra")`).
