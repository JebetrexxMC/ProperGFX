# Third-party notices

TyFMCGFX includes small pieces adapted from the projects below. Each one keeps its own license; the rest of TyFMCGFX is unchanged.

## Google Filament (Apache License 2.0)

- Source: https://github.com/google/filament (fork used: https://github.com/JebetrexxMC/filament)
- Used in: `shaders/fragment/tonemap.glsl` and `src32/ps_shaders.h` (`TM_MODE 4` AgX, `TM_MODE 5` PBR Neutral), and the CPU copies in `src64/ps_fx.cpp` / `src32/ps_fx.cpp` (`TmCpu`).
- What was taken: the AgX tone mapper (matrices, log2 range and the 6th-order sigmoid fit from Filament's `ToneMapper`, based on Troy Sobotka's AgX) and the Khronos PBR Neutral tone mapper.
- Changes: rewritten in GLSL ES 1.00 / 3.00. Input exposure is scaled to match this mod's other tone mappers, and the output is converted to the composite's gamma-2.0 linear space.

Copyright (C) 2015 The Android Open Source Project. Licensed under the Apache License, Version 2.0. You may obtain a copy of the license at http://www.apache.org/licenses/LICENSE-2.0. Distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND.

## dlss5-video-player (MIT License)

- Source: https://github.com/2600th/dlss5-video-player (fork used: https://github.com/JebetrexxMC/dlss5-video-player)
- Used in: `shaders/fragment/def_taa.glsl` (`histCR`).
- What was taken: the 9-tap Catmull-Rom history fetch from its temporal stability pass (`src/TemporalStabilityShader.h`).
- Changes: ported from HLSL to GLSL ES 3.00 and wired into the TAA. None of the DLSS / NGX parts are used.

Copyright (c) 2026 DLSS Video Player contributors. Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.

## prod80 ReShade Repository (MIT License)

- Source: https://github.com/prod80/prod80-ReShade-Repository (fork used: https://github.com/JebetrexxMC/prod80-ReShade-Repository)
- Used in: `shaders/fragment/final.glsl` and `src32/ps_shaders.h` (`uP1`-`uP4`, [ColorBalance] and [Technicolor]).
- What was taken: the Color Balance (shadows / midtones / highlights) and Technicolor 3-strip formulas.
- Changes: ported from ReShade FX to GLSL ES 1.00 / 3.00 and moved into the mod's grading block.

Copyright (c) 2020 prod80. Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.

## Not included (and why)

- NVIDIA DLSS 5, Streamline, Streamline_Sample, DLSS5-Autopilot, dlss5-visual-enhancer. These need an NVIDIA RTX GPU, Windows and Direct3D 12 / Vulkan with NVIDIA's NGX runtime, none of which exist on Android phones. dlss5-visual-enhancer's license also forbids reuse. The mod's VisualEnhance pass is an independent algorithm written for mobile GPUs.
- LibertyRecomp (GPL-3.0). It is a GTA IV PC recompilation. Its game shaders are recompiled from Rockstar's files, and its own code is GPL, so nothing was copied from it.
- Real Shadow Sun (`libRealShadowSun.so` by YtModol). It is closed-source with no license. It was only studied in Ghidra to learn its approach, and no code was copied. [SunShadows] is new code.
- Reshade-Unlocked, URTI, ScarlettPort, glsl-shaders, GLSL and NoodlesPlate. These are Windows/PC/Xbox tools, specifications or GPL desktop software, with no part that fits a GLES Android mod.
