# TYFMC.ProperGFX.ini: sections added in 1.5.0 (64-bit)

The mod writes every key with its default the first time it runs. All of these can also be changed in the menu and through the [API](../API.md).

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

LUT (unchanged since 1.4.2): `[LUT] Folders =` is the folder (empty = `files/ProperGFX` and `files/ProperGFX/luts`), and `File =` is the file name with its extension, for example `File = warm.png`.

Quality presets (Low / Medium / High / Ultra) set these too: Ultra turns on 4 cascades, PCSS, full-resolution SSGI, volumetric clouds, the PBR sky and TAA.
