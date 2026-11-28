# TyFMCGFX 1.6.2 (formerly Proper GFX Mobile): feature status

The 1.5.0 deferred screen-space features (AO / GI, skylight, ambient, contact shadows, volumetrics, clouds, PBR sky, aurora, SSR, rain, caustics, TAA, FSR, depth DoF, 4 cascades + PCSS) are in the **64-bit** build (GTA SA 2.10, GLSL ES 3.00). The 32-bit build (2.00) gets the new menu style, the icon button and the menu fix.

1.6.0 adds the night lighting of ZyZGfx 2.0.7 to the 64-bit build (dynamic lights, every car's headlight beams, moon light / shadows / rays, night look and sky, lens glare, wet streaks, lightning, rain on cars, drying roads, draw distance, streaming memory) and the green Firestorm Preferences menu to both builds.

Legend: ✅ implemented · 🟡 approximated · ❌ not possible on this engine from a mod

| # | Feature | Status | Notes |
|---|---|---|---|
| 1 | Full deferred rendering | 🟡 approximated | 64-bit 1.5.0: deferred **screen-space lighting** (`ps_deferred.cpp`, `def_*.glsl`) on the game's scene colour + readable depth, run on the render thread right after RenderScene. The game itself stays a forward renderer: there is no G-buffer with albedo / roughness, so lighting is rebuilt from depth (position + normal). |
| 2 | Per-pixel lighting with hundreds of lights | 🟡 approximated | 64-bit 1.6.0 `[DynamicLights]`: every lamp / neon / car light / point light the game reports (CShadows + CPointLights hooks, 128-light pool) lights the depth-reconstructed world; the 32 nearest per frame, the 4 nearest with screen-space shadows. |
| 3 | Volumetric light beams | ✅ implemented | 64-bit: sun and moon beams through the shadow map (`def_volume.glsl`) + 1.6.0 `[VolumetricLights]` headlight cones of every car (`CVehicle::DoHeadLightBeam`, 8 nearest). |
| 4 | Shadermaps (normal / roughness / metallic) + POM | ❌ not possible on this engine | Needs per-texture extra maps (TXDP streaming) and tangents in the game's meshes. |
| 5 | Dynamic 4 cascaded shadows (PCSS) | ✅ implemented | 64-bit: `[DynamicShadows] Cascades = 2 / 4`, `PCSS = 1` (blocker search, contact-hardening penumbra). 32-bit: one map. |
| 6 | Soft shadow mask with gaussian blur | ✅ implemented | 64-bit: depth-aware gaussian blur of the mask (`shadow_blur.glsl`, `SoftMaskBlur`). |
| 7 | Screen-space GI with AO | ✅ implemented | 64-bit: `[AmbientOcclusion] Mode = SSGI` (HBAO + one indirect bounce of the occluder colour), half resolution + bilateral upsample. |
| 8 | Skylight (sky occlusion) | 🟡 approximated | 64-bit: large-radius AO that only occludes the sky ambient term. |
| 9 | Tricolored ambient lighting | ✅ implemented | 64-bit: sky / horizon / ground ambient by the reconstructed normal, from the timecyc colours. |
| 10 | Volumetric clouds | 🟡 approximated | 64-bit: 6-step ray-marched cloud slab over the sky pixels (not a full 3D volume). |
| 11 | Volumetric height fog with godrays | ✅ implemented | 64-bit: exponential height fog ray-marched through the sun shadow map (`[VolumetricFog]`). |
| 12 | Ocean waves and boat wakes | ❌ not possible on this engine | Water is the game's own mesh; wave geometry / wakes need engine changes. |
| 13 | Water surface, caustics and underwater | ✅ implemented | Water-detail patch, 64-bit caustics projected onto the sea floor from the depth (`[WaterCaustics]`), underwater tint / distortion. |
| 14 | GPU decals | ❌ not possible on this engine | Needs the game's decal / bullet-hit data on the GPU. |
| 15 | PBR bullet holes | ❌ not possible on this engine | Same as decals. |
| 16 | SSAO, HBAO and SSGI | ✅ implemented | 64-bit: all three modes (`[AmbientOcclusion] Mode`). |
| 17 | HDR tonemapping (ACES / Timothy) | ✅ implemented | ACES / Lottes (Timothy) / Reinhard. |
| 18 | HDR composition with banding fix | ✅ implemented | Triangular dither in the final pass. |
| 19 | LUT | ✅ implemented | `[LUT] Folders =` + `File =` (png / bmp / tga / jpg), live reload. |
| 20 | Auto exposure | ✅ implemented | GPU log-average luma. |
| 21 | Bloom | ✅ implemented | Soft-knee bright pass + separable gaussian. |
| 22 | Sun rays | ✅ implemented | Screen-space radial shafts + 64-bit shadow-map godrays. |
| 23 | SMAA with temporal | 🟡 approximated | FXAA + 64-bit TAA (depth reprojection, neighbourhood clamp, no sub-pixel jitter: the game's projection cannot be jittered safely). No SMAA. |
| 24 | AMD CAS | ✅ implemented | FidelityFX CAS formula. |
| 25 | FSR | 🟡 approximated | 64-bit: FSR 1 style EASU edge reconstruction + RCAS at native resolution (the game cannot render at a lower internal resolution from a mod). |
| 26 | Adaptive DoF | ✅ implemented | 64-bit: depth-based circle of confusion with auto focus (`def_focus.glsl` / `def_coc.glsl`); tilt-shift remains as a mode. |
| 27 | Dithered sky | 🟡 approximated | Global dither in the final pass removes sky banding; the game's sky shader is unchanged. |
| 28 | PBR sky with sky blend | 🟡 approximated | 64-bit: analytic Rayleigh / Mie sky from the sun vector, blended over the game's sky (`[PBRSky] Blend`). |
| 29 | Horizon fog | ✅ implemented | Fog curve patch + 64-bit height fog. |
| 30 | Z-fighting fix | 🟡 approximated | Near-clip override. |
| 31 | Soft particles | ❌ not possible on this engine | Particles are drawn by the game after the world with its own shaders; no hook to fade them by depth. |
| 32 | GPU-instanced grass | ❌ not possible on this engine | Engine geometry / streaming change. |
| 33 | Multithreaded procedural grass / objects | ❌ not possible on this engine | Engine change. |
| 34 | Directional-lit shaders | 🟡 approximated | Specular / reflection multipliers on the game's lit shaders. |
| 35 | Stochastic texturing | ❌ not possible on this engine | Needs per-texture flags in the game's shaders. |
| 36 | Realtime global reflections | 🟡 approximated | 64-bit: screen-space reflections only (`[SSR]`), sky / ambient fallback for misses. |
| 37 | SSR (wet floors and puddles) | ✅ implemented | 64-bit: ray-marched SSR with bisection refinement on wet / flat / shiny surfaces. |
| 38 | World rain surface | ✅ implemented | 64-bit: puddles + ripples on upward surfaces while CWeather::WetRoads > 0 (`[WorldRain]`); wet look on the world shaders. |
| 39 | Rain falling effect | ✅ implemented | 64-bit: screen-space rain streaks (`[RainFall]`). |
| 40 | Vehicle PBR, fresnel and rain | 🟡 approximated | Reflection / specular multipliers + wet look; 64-bit SSR adds fresnel reflections on cars. |
| 41 | Volumetric plane contrails | ❌ not possible on this engine | Needs the plane entities' positions per frame + a particle system. |
| 42 | Animated wind | ❌ not possible on this engine | Vegetation is not tagged in the game's shaders. |
| 43 | Pickup shine | ❌ not possible on this engine | Pickups share generic shaders. |
| 44 | First-person anti-clipping | 🟡 approximated | Near-clip override. |
| 45 | Artistic: VHS | ✅ implemented | Scanlines, tracking noise, chroma bleed. |
| 46 | Artistic: GTA:CW outlines | ✅ implemented | Sobel outline pass. |
| 47 | Artistic: Cel / Anime | ✅ implemented | Screen-space + world-shader quantisation. |
| 48 | Artistic: Fur | ❌ not possible on this engine | Needs shell geometry per model. |
| 49 | Artistic: Aurora | ✅ implemented | 64-bit: borealis on night sky pixels (`[Aurora]`). |
| 50 | Unlimited TXDPs | ❌ not possible on this engine | IDE / streaming parser hooks (separate mod). |
| 51 | 11k textures configured | ❌ not possible on this engine | PC data pack, not code. |
| 52 | ModLoader implementation | ✅ implemented | AML mod; LUTs / custom.glsl / ini load from `files/TyFMCGFX/`. |
| 53 | Public API for modders | ✅ implemented | 64-bit: TyFMCGFX API (`src64/propergfx_api.h`), AML interface `TyFMCGFX` + exported C functions (see API.md). |
| 54 | In-game GUI editor | ✅ implemented | Firestorm Preferences menu (green accents), search box with on-screen keyboard, every setting live, presets 1-8, OK / Cancel. |
| 55 | Lens flare | 🟡 approximated | Screen-space ghosts / halo. |
| 56 | Anisotropic hair / SSS / terrain splat maps | ❌ not possible on this engine | Need per-material shaders and data. |
| 57 | Contact shadows | ✅ implemented | 64-bit: short screen-space rays toward the sun. |
| 58 | Volumetric headlights (experimental) | 🟡 approximated | 64-bit: light cones from the player's vehicle at night. |
| 59 | Foliage backlights | ❌ not possible on this engine | Foliage cannot be told apart in a screen-space pass. |
| 60 | Relief mapping | ❌ not possible on this engine | Same as POM. |
| 61 | Floating settings button | ✅ implemented | 1.6.0: Firestorm status-bar pill (clock, icon, FPS, red end cap); shrinks to the icon when idle; `[Button] Lock`. |
| 62 | No effects in main / pause / floating menu | ✅ implemented | 1.5.0: effects run inside the frame (after the scene / before CPostEffects::MobileRender, below the HUD) and stop while CTimer::m_UserPause or m_CodePause is set. |
| 63 | Moon light, moon shadows, moon rays, sea glitter | ✅ implemented | 64-bit 1.6.0 `[Moon]` (ZyZ 2.0 "moon" module). |
| 64 | Night look and night sky | ✅ implemented | 64-bit 1.6.0 `[NightLook]` / `[NightSky]`: night grade, lamp / neon / fog glow, city glow, stars, Milky Way, shooting stars. |
| 65 | Lens glare and wet light streaks | ✅ implemented | 64-bit 1.6.0 `[LensGlare]`, `[WetLightStreaks]`. |
| 66 | Lightning and drying roads | ✅ implemented | 64-bit 1.6.0 `[Weather]`: `CWeather::LightningFlash` lights the scene; wetness fades over `DryingTime`. |
| 67 | Rain on cars | 🟡 approximated | 64-bit 1.6.0 `[RainOnCars]`: bead pattern + wet film in the vehicle env-map shader (only shaders with `Out_Tex0`). |
| 68 | Draw distance / streaming memory | ✅ implemented | 64-bit 1.6.0 `[Performance]`. |
| 69 | Lit / living windows, PBR material highlights, render scale (ZyZ 2.0) | ❌ not ported | Need ZyZ's own per-model texture tables / a lower-resolution scene target the game does not provide; left out on purpose. |

**Totals:** 35 implemented, 16 approximated, 18 not possible / not ported (out of 69).
