# TyFMCGFX.ini: sections added in 1.5.0 and 1.6.0 (64-bit)

The settings file is `files/TyFMCGFX/TyFMCGFX.ini` (1.4-1.5: `files/ProperGFX/TYFMC.ProperGFX.ini`, moved and renamed automatically on the first 1.6.0 launch). The mod writes every key with its default the first time it runs. All of these can also be changed in the menu and through the [API](../API.md).

| Section | Key | Default | Meaning |
|---|---|---|---|
| Deferred | Enabled | 1 | Master switch of all screen-space lighting below |
| Deferred | HalfRes | 1 | AO / GI / volumetrics at half resolution (bilateral upsample) |
| AmbientOcclusion | Enabled / Mode | 1 / HBAO | `SSAO`, `HBAO` or `SSGI` (HBAO + one indirect colour bounce) |
| AmbientOcclusion | Radius / Intensity / GlobalIllumination | 1.2 / 0.85 / 0.6 | Radius in metres, strength, SSGI bounce strength |
| Skylight | Enabled / Strength | 1 / 0.55 | Sky occlusion (large-scale AO on the sky ambient only) |
| AmbientLighting | Enabled / Strength | 1 / 0.30 | Tricoloured ambient: sky / horizon / ground by the surface normal |
| ContactShadows | Enabled / Length / Strength | 1 / 0.8 / 0.6 | Short screen-space shadow rays toward the sun |
| VolumetricFog | Enabled / Density(.Day/.Night) / HeightFalloff / Godrays | 1 / 0.30, 0.45 / 0.35 / 0.8 | Height fog + god rays through the shadow map |
| VolumetricHeadlights | Enabled / Strength | 0 / 0.6 | Experimental: light cones of the player's vehicle at night |
| VolumetricClouds | Enabled / Coverage / Density / Speed | 0 / 0.45 / 0.8 / 1.0 | Ray-marched cloud layer over the sky |
| PBRSky | Enabled / Blend | 0 / 0.45 | Rayleigh / Mie sky from the sun vector, blended over the game's sky |
| Aurora | Enabled / Strength | 0 / 0.6 | Borealis on the night sky |
| SSR | Enabled / Strength / DrySurfaces | 1 / 0.55 / 0.12 | Screen-space reflections (wet floors, puddles, flat shiny surfaces) |
| WorldRain | Enabled / Puddles / Ripples | 1 / 0.6 / 0.6 | Puddles and ripples on upward surfaces while it rains |
| RainFall | Enabled / Intensity | 1 / 0.5 | Falling rain streaks |
| WaterCaustics | Enabled / Strength | 1 / 0.6 | Caustics projected onto the sea floor |
| TAA | Enabled / Blend | 0 / 0.86 | Temporal anti-aliasing (history weight) |
| FSR | Enabled / Sharpness | 0 / 0.35 | FSR 1 style edge reconstruction + RCAS sharpening |
| DynamicShadows | Cascades | 2 | 2 or 4 shadow cascades |
| DynamicShadows | PCSS | 1 | Contact-hardening soft shadows (blocker search) |
| DynamicShadows | SoftMaskBlur | 1 | Gaussian blur of the shadow mask |


## Added in 1.6.0 (ported from ZyZGfx 2.0.7)

| Section | Key | Default | Meaning |
|---|---|---|---|
| DynamicLights | Enabled / Strength / Range | 1 / 1.0 / 1.0 | Street lamps, neon, building lights, car lamps and point lights (`CShadows::StoreStaticShadow`, `StoreCarLightShadow`, `CPointLights::AddLight`) light the world per pixel, the 32 nearest each frame |
| DynamicLights | Shadows / ShadowDebug | 1 / 0 | Screen-space shadows of the 4 nearest lights / tint lit areas |
| DynamicLights | ReplaceGameShadows | 0 | Skip the game's flat additive light pools (type-2 static shadows) |
| VolumetricLights | Enabled / BeamStrength / ReplaceGameBeams | 1 / 0.8 / 0 | Headlight cones of every car (`CVehicle::DoHeadLightBeam`), ray-marched through the fog (8 nearest) |
| LensGlare | Enabled / Strength | 1 / 0.6 | Star streaks on bright lights |
| WetLightStreaks | Enabled / Strength | 1 / 0.8 | Long vertical reflections of lights on wet roads |
| Moon | Light / LightStrength | 1 / 0.5 | Directional moon light at night |
| Moon | Shadows / ShadowStrength | 1 / 0.55 | Dynamic shadows follow the moon at night |
| Moon | Rays / RaysStrength, Glitter / GlitterStrength | 1 / 0.5, 1 / 0.6 | Moon god rays through the fog, moon glitter on the sea |
| NightLook | Enabled / Contrast / Exposure / ColourShift | 1 / 1.08 / 0.15 / 0.35 | Night grade (blue shift, contrast, exposure) |
| NightLook | Bloom / LampGlow / NeonGlow / FogGlow | 0.3 / 0.5 / 0.45 / 0.5 | Extra night bloom, halos around lamps / neon, light in-scatter in fog |
| NightSky | CityGlow / CityGlowStrength | 1 / 0.5 | Orange city light on the night sky and clouds |
| NightSky | Stars / StarsStrength / MilkyWay / ShootingStars | 1 / 0.7 / 0.5 / 0.5 | Procedural night sky |
| AmbientOcclusion | MultiBounce / GIQuality | 1 / 1 | GTAO multi-bounce; GI sample count 0 low / 1 medium / 2 high |
| Weather | Lightning / LightningStrength | 1 / 0.8 | Lightning flashes light up the world (`CWeather::LightningFlash`) |
| Weather | Drying / DryingTime | 1 / 90 | Roads dry slowly after rain (seconds) |
| RainOnCars | Enabled / Quality | 1 / 1 | Rain beads and wet paint on vehicles (0 medium / 1 high / 2 ultra) |
| Performance | DrawDistance | 1.0 | Far-clip multiplier (clamped 50-5000 m, like ZyZ) |
| Performance | StreamingMemory | 0 | Streaming memory in MB (0 = game default), applied at load |
| Menu | PauseGame | 0 | Pause the game while the menu is open |
| Button | Lock | 0 | The floating button can not be dragged (both builds) |

LUT (unchanged since 1.4.2): `[LUT] Folders =` is the folder (empty = `files/TyFMCGFX` and `files/TyFMCGFX/luts`), and `File =` is the file name with its extension, for example `File = warm.png`.

Quality presets (Low / Medium / High / Ultra) set these too: Ultra turns on 4 cascades, PCSS, full-resolution SSGI, volumetric clouds, the PBR sky and TAA.
