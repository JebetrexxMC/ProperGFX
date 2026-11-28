// Proper Shader Mobile - GLSL ES 1.00 sources (GLES2). The final composite is compiled as a *variant*:
// every feature that is switched off is removed at compile time, so it costs nothing on the GPU.
#pragma once

static const char* kVS =
"attribute vec2 aPos;\n"
"varying vec2 vUV;\n"
"void main(){ vUV = aPos*0.5+0.5; gl_Position = vec4(aPos,0.0,1.0); }\n";

static const char* kFSHead = R"GLSL(
#ifdef GL_FRAGMENT_PRECISION_HIGH
#define HP highp
#else
#define HP mediump
#endif
precision mediump float;
varying HP vec2 vUV;
)GLSL";

// FXAA 1.0 (T. Lottes, public domain) - shared by the stand-alone pass and the inlined variant
static const char* kFXAAFunc = R"GLSL(
float fxLum(vec3 c){ return dot(c, vec3(0.299,0.587,0.114)); }
vec3 fxaaSample(sampler2D tex, vec2 uv, vec2 t, float strength){
  vec3 rgbM  = texture2D(tex, uv).rgb;
  vec3 rgbNW = texture2D(tex, uv + vec2(-1.0,-1.0)*t).rgb;
  vec3 rgbNE = texture2D(tex, uv + vec2( 1.0,-1.0)*t).rgb;
  vec3 rgbSW = texture2D(tex, uv + vec2(-1.0, 1.0)*t).rgb;
  vec3 rgbSE = texture2D(tex, uv + vec2( 1.0, 1.0)*t).rgb;
  float lNW = fxLum(rgbNW), lNE = fxLum(rgbNE), lSW = fxLum(rgbSW), lSE = fxLum(rgbSE), lM = fxLum(rgbM);
  float lMin = min(lM, min(min(lNW,lNE), min(lSW,lSE)));
  float lMax = max(lM, max(max(lNW,lNE), max(lSW,lSE)));
  if (lMax - lMin < max(0.0312, lMax * 0.125)) return rgbM;     // flat area: no edge, skip the 4 extra fetches
  vec2 dir;
  dir.x = -((lNW + lNE) - (lSW + lSE));
  dir.y =  ((lNW + lSW) - (lNE + lSE));
  float dirReduce = max((lNW + lNE + lSW + lSE) * (0.25 * 0.125), 1.0/128.0);
  float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
  dir = min(vec2(8.0), max(vec2(-8.0), dir * rcpDirMin)) * t;
  vec3 rgbA = 0.5 * (texture2D(tex, uv + dir*(1.0/3.0-0.5)).rgb + texture2D(tex, uv + dir*(2.0/3.0-0.5)).rgb);
  vec3 rgbB = rgbA*0.5 + 0.25*(texture2D(tex, uv + dir*-0.5).rgb + texture2D(tex, uv + dir*0.5).rgb);
  float lB = fxLum(rgbB);
  vec3 res = (lB < lMin || lB > lMax) ? rgbA : rgbB;
  return mix(rgbM, res, strength);
}
)GLSL";

static const char* kFSAAMain = R"GLSL(
uniform sampler2D uTex; uniform vec4 uRes; uniform vec4 uP;
void main(){ gl_FragColor = vec4(fxaaSample(uTex, vUV, uRes.zw, uP.x), 1.0); }
)GLSL";

// 4x downsample + soft-knee bright pass  (uP: x threshold, y knee)
static const char* kFSBright = R"GLSL(
uniform sampler2D uTex; uniform vec4 uRes; uniform vec4 uP;
void main(){
  vec2 t = uRes.zw;
  vec3 c = texture2D(uTex, vUV + t*vec2(-1.0,-1.0)).rgb + texture2D(uTex, vUV + t*vec2(1.0,-1.0)).rgb
         + texture2D(uTex, vUV + t*vec2(-1.0,1.0)).rgb  + texture2D(uTex, vUV + t*vec2(1.0,1.0)).rgb;
  c *= 0.25;
  float br = max(c.r, max(c.g, c.b));
  float knee = uP.y + 0.0001;
  float soft = clamp(br - uP.x + knee, 0.0, 2.0*knee);
  soft = soft*soft/(4.0*knee);
  float contrib = max(soft, br - uP.x) / max(br, 0.0001);
  gl_FragColor = vec4(c*contrib, 1.0);
}
)GLSL";

// plain 4x downsample (source for the tilt-shift blur)
static const char* kFSDown = R"GLSL(
uniform sampler2D uTex; uniform vec4 uRes;
void main(){
  vec2 t = uRes.zw;
  vec3 c = texture2D(uTex, vUV + t*vec2(-1.0,-1.0)).rgb + texture2D(uTex, vUV + t*vec2(1.0,-1.0)).rgb
         + texture2D(uTex, vUV + t*vec2(-1.0,1.0)).rgb  + texture2D(uTex, vUV + t*vec2(1.0,1.0)).rgb;
  gl_FragColor = vec4(c*0.25, 1.0);
}
)GLSL";

// 9-tap gaussian from 5 bilinear fetches (uDir.xy = UV-space step)
static const char* kFSBlur = R"GLSL(
uniform sampler2D uTex; uniform vec4 uDir;
void main(){
  vec3 c = texture2D(uTex, vUV).rgb * 0.2270270270;
  vec2 o1 = uDir.xy * 1.3846153846;
  vec2 o2 = uDir.xy * 3.2307692308;
  c += (texture2D(uTex, vUV + o1).rgb + texture2D(uTex, vUV - o1).rgb) * 0.3162162162;
  c += (texture2D(uTex, vUV + o2).rgb + texture2D(uTex, vUV - o2).rgb) * 0.0702702703;
  gl_FragColor = vec4(c, 1.0);
}
)GLSL";

// GPU-only auto exposure: 16-tap log-average luma, smoothed over time in a 1x1 RGBA8 target (16-bit packed in RG)
static const char* kFSLum = R"GLSL(
uniform sampler2D uTex; uniform sampler2D uPrev; uniform vec4 uL; // x adapt, y reset
void main(){
  float s = 0.0;
  for (int j = 0; j < 4; j++) {
    for (int i = 0; i < 4; i++) {
      vec2 p = vec2(0.1 + 0.8*(float(i)+0.5)/4.0, 0.1 + 0.8*(float(j)+0.5)/4.0);
      vec3 c = texture2D(uTex, p).rgb;
      s += log(dot(c*c, vec3(0.2126,0.7152,0.0722)) + 0.002);
    }
  }
  float cur = exp(s*(1.0/16.0));
  vec4 pk = texture2D(uPrev, vec2(0.5));
  float prev = dot(pk.rg, vec2(1.0, 1.0/255.0));
  float v = mix(prev, cur, uL.x);
  if (uL.y > 0.5) v = cur;
  v = clamp(v, 0.0, 0.9999);
  vec2 enc = vec2(v, fract(v*255.0));
  enc.x -= enc.y/255.0;
  gl_FragColor = vec4(enc, 0.0, 1.0);
}
)GLSL";

// ---- final composite: compiled per feature mask (see FX_* defines prepended by the host) ----
static const char* kFSFinal = R"GLSL(
uniform sampler2D uScene; uniform sampler2D uBloom; uniform sampler2D uDof; uniform sampler2D uLum; uniform sampler2D uLut;
uniform vec4 uRes;
uniform HP vec4 uA;   // time, ev, hdrK, (unused)
uniform vec4 uB;      // contrast, saturation, vibrance, gamma
uniform vec4 uC;      // temperature, tint, lift, gain
uniform vec4 uD;      // bloom, rays, flare, (unused)
uniform vec4 uE;      // vignette, vigSize, grain, chromatic
uniform vec4 uF;      // cas, tilt, underwater, lut
uniform vec4 uG;      // celAmt, celLevels, outline, outlineThr
uniform vec4 uH;      // vhs, tiltFocus, tiltWidth, (unused)
uniform vec4 uSun;    // x, y, visibility, decay
uniform vec4 uK;      // autoBias, autoRange, tonemapNorm, frame
uniform vec4 uGS;     // grade shadow tint
uniform vec4 uGH;     // grade highlight tint, w = fxaa strength
uniform vec4 uEN;     // enhance: clarity, (unused), denoise, radius px
uniform vec4 uTM;     // 1.6.2 tone mapping: strength, contrast, saturation, white point
uniform vec4 uP1; uniform vec4 uP2; uniform vec4 uP3; uniform vec4 uP4;   // 1.6.2 prod80 colour balance + Technicolor

float lum3(vec3 c){ return dot(c, vec3(0.299,0.587,0.114)); }
vec3 toLin(vec3 c){ return c*c; }                 // gamma 2.0 approximation: 1 MUL instead of pow()
vec3 toGam(vec3 c){ return sqrt(max(c, vec3(0.0))); }
HP float ign(HP vec2 p){ return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }
HP float hash21(HP vec2 p){ return fract(sin(dot(p, vec2(12.9898,78.233))) * 43758.5453); }

#ifdef FX_DOF
// 1.6.1 real depth of field: uCamDepth = this frame's world from the camera (ps_shadow.cpp camera depth pass, RG = log depth, B = geometry).
// Thin-lens blur |z - focus| / z with a sharp zone, auto focus on the screen centre, 12-tap golden-angle gather from the full-res scene in which
// every tap is weighted by its own blur (a sharp object never smears into the blurred background), big blurs read the pre-blurred copy.
uniform sampler2D uCamDepth;
uniform vec4 uDofP;   // mode, manual focus (m, 0 = auto), aperture, radius
uniform vec4 uDofQ;   // bokeh, strength, sharp zone (focus width), 0
HP float dofZ(vec2 uv){ vec4 c = texture2D(uCamDepth, uv); if (c.b < 0.5) return 3000.0; return exp2((c.r * 255.0 * 256.0 + c.g * 255.0) / 65535.0 * 11.0) - 1.0; }
vec2 dofCoc(HP float z, HP float f){
  HP float zone = 0.5 + uDofQ.z * 8.0 + f * 0.15;
  HP float far  = max(z - f - zone, 0.0) / max(z, 0.1);
  HP float near = max(f - zone * 0.5 - z, 0.0) / max(z, 0.1);
  float cf = clamp(far * (0.35 + uDofQ.y * 0.65) * uDofP.z * 2.2, 0.0, 1.0);
  float cn = clamp(near * (0.6 + uDofQ.y * 0.6) * uDofP.z * 1.4, 0.0, 1.0);
  return vec2(max(cf, cn), cn);
}
vec3 dofDepth(vec3 col, vec2 uv, vec2 px){
  HP float f = uDofP.y;
  if (f < 0.05) {                                 // auto focus: what is in the screen centre (nearer taps win a bit)
    HP float zs = 0.0, ws = 0.0;
    for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {
      HP float l = min(dofZ(vec2(0.5) + vec2(float(x), float(y)) * 0.04), 1500.0);
      HP float w = 1.0 / (1.0 + l * 0.02); zs += l * w; ws += w;
    }
    f = zs / ws;
  }
  f = max(f, 0.3);
  float rmax = 10.0 * uDofP.w * (uRes.y / 720.0) * clamp(uDofQ.y * 1.25, 0.0, 1.5);
  vec2 c0 = dofCoc(dofZ(uv), f);
  float nd = c0.y;
  for (int k = 0; k < 4; k++) { float a = float(k) * 1.5707963 + 0.785398; nd = max(nd, dofCoc(dofZ(uv + vec2(cos(a), sin(a)) * px * rmax * 0.6), f).y * 0.85); }
  float cc = max(c0.x, nd);
  float rad = cc * rmax;
  if (rad < 0.6) return col;
  float big = clamp((rad - 8.0) / 9.0, 0.0, 1.0);
  float rot = ign(gl_FragCoord.xy) * 6.2831853;
  vec3 acc = col; float wsum = 1.0;
  for (int i = 1; i <= 12; i++) {
    float fi = float(i);
    float r = sqrt(fi / 12.0) * rad;
    float a = fi * 2.3999632 + rot;
    vec2 tuv = uv + vec2(cos(a), sin(a)) * r * px;
    vec3 c = mix(texture2D(uScene, tuv).rgb, texture2D(uDof, tuv).rgb, big);
    vec2 tc = dofCoc(dofZ(tuv), f);
    float w = clamp(max(tc.x, tc.y) * rmax - r + 1.5, 0.0, 1.0);
    float l = lum3(c);
    w *= 1.0 + uDofQ.x * l * l * 4.0;
    acc += c * w; wsum += w;
  }
  return mix(col, acc / wsum, smoothstep(0.0, 0.12, cc));
}
#endif

#if TM_MODE == 1
vec3 tonemap(vec3 x){ x *= 0.7232; return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0); }
#elif TM_MODE == 2
vec3 tonemap(vec3 x){ x = max(x*0.7203, vec3(0.0001)); return pow(x, vec3(1.6)) / (pow(x, vec3(1.5632))*1.07304 + 0.16742); }
#elif TM_MODE == 3
vec3 tonemap(vec3 x){ x *= 1.1998; return x*(1.0 + x/16.0)/(1.0 + x); }
#elif TM_MODE == 4
// AgX (Troy Sobotka's AgX, minimal fit). Ported from Google Filament's ToneMapper (Apache-2.0, see THIRD_PARTY_NOTICES.md)
vec3 agxCurve(vec3 x){ vec3 x2 = x*x; vec3 x4 = x2*x2; return 15.5*x4*x2 - 40.14*x4*x + 31.96*x4 - 6.868*x2*x + 0.4298*x2 + 0.1191*x - 0.00232; }
vec3 tonemap(vec3 c){
  const mat3 agxIn  = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992, 0.878468636469772, 0.0784336, 0.0792237451477643, 0.0791661274605434, 0.879142973793104);
  const mat3 agxOut = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368, 1.15190312990417, -0.0980434501171241, -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
  c = agxIn * (c * 0.62);
  c = clamp(log2(max(c, vec3(1e-10))), -12.47393, 4.026069);
  c = (c + 12.47393) / 16.5;
  c = agxOut * agxCurve(c);
  c = max(c, vec3(0.0));
  return c * c;                                    // AgX output is display-encoded: back to the composite's linear (gamma 2.0)
}
#elif TM_MODE == 5
// Khronos PBR Neutral. Ported from Google Filament's ToneMapper (Apache-2.0, see THIRD_PARTY_NOTICES.md)
vec3 tonemap(vec3 c){
  const float startC = 0.76; const float desat = 0.15;
  float x = min(c.r, min(c.g, c.b));
  float off = x < 0.08 ? x - 6.25*x*x : 0.04;
  c -= off;
  float peak = max(c.r, max(c.g, c.b));
  if (peak < startC) return c;
  const float d = 1.0 - startC;
  float np = 1.0 - d*d / (peak + d - startC);
  c *= np / peak;
  float g = 1.0 - 1.0 / (desat * (peak - np) + 1.0);
  return mix(c, vec3(np), g);
}
#endif

#ifdef FX_AA
FXAA_FUNC
#endif

#ifdef FX_LUT
// generic LUT: strip (256x16, 1024x32, 4096x64) or square tile atlas (64x64, 512x512). uLutP = (cube size N, tiles per row, 1/width, 1/height).
// The loader stores green growing with v inside every tile and the tiles left-to-right, top-to-bottom by blue.
uniform HP vec4 uLutP;
vec3 applyLut(vec3 c){
  HP vec3 cc = clamp(c, 0.0, 1.0);
  HP float n = uLutP.x; HP float tpr = uLutP.y;
  HP float b = cc.b * (n - 1.0); HP float s0 = floor(b); HP float s1 = min(s0 + 1.0, n - 1.0); HP float f = b - s0;
  HP vec2 inTile = vec2(cc.r * (n - 1.0) + 0.5, cc.g * (n - 1.0) + 0.5);
  HP float r0 = floor((s0 + 0.5) / tpr); HP float r1 = floor((s1 + 0.5) / tpr);
  HP vec2 t0 = vec2(s0 - r0 * tpr, r0) * n; HP vec2 t1 = vec2(s1 - r1 * tpr, r1) * n;
  return mix(texture2D(uLut, (t0 + inTile) * uLutP.zw).rgb, texture2D(uLut, (t1 + inTile) * uLutP.zw).rgb, f);
}
#endif

#ifdef FX_RAYS
vec3 sunShafts(vec2 uv){
  vec2 stepv = (uSun.xy - uv) * (0.9/12.0);
  vec2 p = uv; float w = 1.0; vec3 acc = vec3(0.0);
  for (int i = 0; i < 12; i++) { p += stepv; acc += texture2D(uBloom, p).rgb * w; w *= uSun.w; }
  float fall = clamp(1.4 - length(uSun.xy - uv), 0.0, 1.0);
  return acc * (1.0/12.0) * uSun.z * fall;
}
#endif

#ifdef FX_FLARE
vec3 flare(vec2 uv){
  vec2 gv = (vec2(0.5) - uv) * 0.45;
  vec3 acc = vec3(0.0);
  for (int i = 0; i < 4; i++) {
    vec2 o = fract(uv + gv * float(i + 1));
    float wgt = pow(clamp(1.0 - length(vec2(0.5) - o)/0.7071, 0.0, 1.0), 6.0);
    vec3 tintc = vec3(1.0 - 0.2*float(i), 0.75 + 0.1*float(i), 0.6 + 0.2*float(i));
    acc += texture2D(uBloom, o).rgb * wgt * tintc;
  }
  vec2 hv = normalize(gv + vec2(0.0001)) * 0.38;
  float hw = pow(clamp(1.0 - length(vec2(0.5) - (uv + hv))/0.7071, 0.0, 1.0), 4.0);
  acc += texture2D(uBloom, uv + hv).rgb * hw;
  return acc;
}
#endif

void main(){
  vec2 uv = vUV;
  vec2 px = uRes.zw;
  vec2 suv = uv;
#if defined(FX_UNDER) || defined(FX_VHS)
  HP float t = uA.x;
#endif
#ifdef FX_UNDER
  suv += vec2(sin(uv.y*16.0 + t*1.9), cos(uv.x*12.0 + t*1.4)) * 0.0045 * uF.z;
#endif
#ifdef FX_VHS
  suv.x += sin(uv.y*uRes.y*0.35 + t*14.0) * 0.0006 * uH.x;
  float bandY = fract(t*0.11);
  float inBand = step(abs(uv.y - bandY), 0.035);
  suv.x += inBand * (hash21(vec2(floor(uv.y*uRes.y*0.5), floor(t*24.0))) - 0.5) * 0.02 * uH.x;
#endif

  vec3 col;
#ifdef FX_CA
  {
    float ca = uE.w * 0.012;
#ifdef FX_VHS
    ca += uH.x * 0.0035;
#endif
    vec2 d = (suv - 0.5) * ca;
    col = vec3(texture2D(uScene, suv + d).r, texture2D(uScene, suv).g, texture2D(uScene, suv - d).b);
  }
#elif defined(FX_AA)
  col = fxaaSample(uScene, suv, px, uGH.w);
#else
  col = texture2D(uScene, suv).rgb;
#endif

#ifdef FX_CAS   // AMD FidelityFX CAS, 5-tap cross variant (4 extra fetches instead of 8)
  {
    vec3 b = texture2D(uScene, suv + px*vec2( 0.0,-1.0)).rgb;
    vec3 d = texture2D(uScene, suv + px*vec2(-1.0, 0.0)).rgb;
    vec3 e = col;
    vec3 f = texture2D(uScene, suv + px*vec2( 1.0, 0.0)).rgb;
    vec3 h = texture2D(uScene, suv + px*vec2( 0.0, 1.0)).rgb;
    vec3 mn = min(min(min(d,e),min(f,b)),h);
    vec3 mx = max(max(max(d,e),max(f,b)),h);
    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(0.0001)), 0.0, 1.0));
    float peak = -1.0 / mix(8.0, 5.0, clamp(uF.x, 0.0, 1.0));
    vec3 w = amp * peak;
    col = clamp(((b + d + f + h) * w + e) / (1.0 + 4.0*w), 0.0, 1.0);
  }
#endif

#ifdef FX_ENHANCE   // TyFMCGFX Visual Enhance (written for this mod): edge-aware denoise + luminance clarity
  {
    vec2 r = px * uEN.w;
    vec3 a0 = texture2D(uScene, suv + r*vec2( 1.0, 1.0)).rgb;
    vec3 a1 = texture2D(uScene, suv + r*vec2(-1.0, 1.0)).rgb;
    vec3 a2 = texture2D(uScene, suv + r*vec2( 1.0,-1.0)).rgb;
    vec3 a3 = texture2D(uScene, suv + r*vec2(-1.0,-1.0)).rgb;
    vec3 base = (a0 + a1 + a2 + a3) * 0.25;
    float lc = lum3(col); float lb = lum3(base);
    float dn = uEN.z * (1.0 - smoothstep(0.012, 0.05, abs(lc - lb)));   // only flat noisy areas, edges stay sharp
    col = mix(col, base, dn * 0.5);
    lc = lum3(col);
    float dl = clamp(lc - lb, -0.10, 0.10) * uEN.x * (4.0 * lc * (1.0 - lc));
    col = clamp(col * ((lc + dl) / max(lc, 0.002)), 0.0, 1.0);
  }
#endif

#ifdef FX_DOF
  if (uDofP.x > 2.5) { }                         // real depth wanted, no depth this frame: no blur
  else if (uDofP.x > 1.5) col = dofDepth(col, suv, px);
  else {
    float m;
    if (uDofP.x < 0.5) m = smoothstep(uH.z, uH.z + 0.28, abs(uv.y - uH.y));
    else { vec2 d = (uv - vec2(0.5, uH.y)) * vec2(uRes.x / uRes.y, 1.0); m = smoothstep(uH.z, uH.z + 0.45, length(d)); }
    col = mix(col, texture2D(uDof, suv).rgb, m * uF.y);
  }
#endif

#ifdef FX_OUTLINE   // screen-space outlines (Sobel on luma)
  {
    float tl = lum3(texture2D(uScene, suv + px*vec2(-1.0,-1.0)).rgb);
    float tt = lum3(texture2D(uScene, suv + px*vec2( 0.0,-1.0)).rgb);
    float tr = lum3(texture2D(uScene, suv + px*vec2( 1.0,-1.0)).rgb);
    float ml = lum3(texture2D(uScene, suv + px*vec2(-1.0, 0.0)).rgb);
    float mr = lum3(texture2D(uScene, suv + px*vec2( 1.0, 0.0)).rgb);
    float bl = lum3(texture2D(uScene, suv + px*vec2(-1.0, 1.0)).rgb);
    float bb = lum3(texture2D(uScene, suv + px*vec2( 0.0, 1.0)).rgb);
    float br = lum3(texture2D(uScene, suv + px*vec2( 1.0, 1.0)).rgb);
    float gx = -tl - 2.0*ml - bl + tr + 2.0*mr + br;
    float gy = -tl - 2.0*tt - tr + bl + 2.0*bb + br;
    float edge = smoothstep(uG.w, uG.w + 0.18, length(vec2(gx, gy)));
    col = mix(col, vec3(0.02,0.015,0.03), edge * uG.z);
  }
#endif

#ifdef FX_CEL
  {
    float l = lum3(col);
    float q = floor(l*uG.y + 0.5) / uG.y;
    col = mix(col, clamp(col * ((q + 0.04) / (l + 0.04)), 0.0, 1.0), uG.x);
  }
#endif

  // ---- pseudo-HDR, exposure, bloom, tone mapping ----
  vec3 lin = toLin(col);
#if TM_MODE > 0
  lin *= 1.0 + uA.z * lin * lin;
#endif
  float ev = uA.y;
#ifdef FX_AUTOEXP
  {
    float avg = dot(texture2D(uLum, vec2(0.5)).rg, vec2(1.0, 1.0/255.0));
    ev += clamp(log2(0.18 / max(avg, 0.004)) * 0.6, -uK.y, uK.y) + uK.x;
  }
#endif
  lin *= exp2(ev);
#ifdef FX_BLOOM
  lin += toLin(texture2D(uBloom, uv).rgb) * (uD.x * 1.6);
#endif
#ifdef FX_RAYS
  lin += toLin(sunShafts(uv)) * (uD.y * 2.2);
#endif
#ifdef FX_FLARE
  lin += toLin(flare(uv)) * (uD.z * 1.4);
#endif
#if TM_MODE > 0
  {   // 1.6.2 [ToneMapping] menu settings: strength (blend with the plain clipped image), contrast around mid grey, saturation
    vec3 pre = clamp(lin, 0.0, 1.0);
    lin = mix(pre, tonemap(lin) * uK.z, uTM.x);
    lin = 0.18 * pow(max(lin, vec3(0.0)) / 0.18, vec3(uTM.y));
    lin = mix(vec3(dot(lin, vec3(0.2126, 0.7152, 0.0722))), lin, uTM.z);
  }
#else
  lin = clamp(lin, 0.0, 1.0);
#endif
  col = toGam(clamp(lin, 0.0, 1.0));

  // ---- grading ----
#ifdef FX_GRADE
  {
    float l0 = lum3(col);
    col += uGS.rgb * (1.0 - l0)*(1.0 - l0) + uGH.rgb * l0*l0;
    col *= vec3(1.0 + 0.10*uC.x, 1.0 - 0.08*uC.y, 1.0 - 0.10*uC.x);
    float l1 = lum3(col);
    col = mix(vec3(l1), col, uB.y);
    float mxc = max(col.r, max(col.g, col.b)); float mnc = min(col.r, min(col.g, col.b));
    col = mix(vec3(l1), col, 1.0 + uB.z * (1.0 - (mxc - mnc)));
    col = (col - 0.5) * uB.x + 0.5;
    col = col * uC.w + uC.z;
#ifdef FX_GAMMA
    col = pow(max(col, vec3(0.0)), vec3(1.0/uB.w));
#endif
  }
#endif
#ifdef FX_GRADE
  // 1.6.2 prod80 ports (MIT, prod80-ReShade-Repository): PD80_04_Color_Balance (shadows / midtones / highlights shift, separation mode 0)
  // and PD80_04_Technicolor (3 strip). uP1..3 rgb = shadow / mid / highlight shift, uP1.w = Technicolor saturation,
  // uP4 = (Technicolor strength, colour balance on, colour strength, brightness). Off = one uniform compare each.
  if (uP4.y > 0.0) {
    vec3 c = clamp(col, 0.0, 1.0);
    float pl = dot(c, vec3(0.333333, 0.333334, 0.333333));
    vec3 ds = max(1.0 - c * 2.0, 0.0); ds = ds * ds * (3.0 - 2.0 * ds);
    vec3 dh = max((c - 0.5) * 2.0, 0.0); dh = dh * dh * (3.0 - 2.0 * dh);
    vec3 mids = clamp(1.0 - ds - dh, 0.0, 1.0);
    col = clamp(c + c * (ds * uP1.rgb + mids * uP2.rgb) * (1.0 - c) + dh * (uP3.rgb * (1.0 - pl)), 0.0, 1.0);
  }
  if (uP4.x > 0.0) {
    vec3 o = clamp(col, 0.0, 1.0);
    vec3 tp = 1.0 - o;
    vec3 t2 = o * tp.grg * tp.bbr;
    tp = t2 * uP4.z; t2 *= uP4.w;
    vec3 r = o - tp.yxy + t2 - tp.zzx;
    r = mix(vec3(dot(r, vec3(0.212656, 0.715158, 0.072186))), r, uP1.w);
    col = mix(o, r, uP4.x);
  }
#endif
#ifdef FX_LUT
  col = mix(col, applyLut(col), uF.w);
#endif

#ifdef FX_UNDER   // underwater tint + caustic shimmer
  {
    vec2 cp = uv*vec2(uRes.x/uRes.y, 1.0)*7.0;
    float cs = sin(cp.x + t*0.9 + sin(cp.y*1.3 + t*0.7)) * sin(cp.y*1.1 - t*0.8 + sin(cp.x*0.9 + t*0.5));
    cs = clamp(cs*0.5 + 0.5, 0.0, 1.0); cs *= cs; cs = cs*cs*cs;
    vec3 wc = col * vec3(0.55, 0.85, 1.0) + vec3(0.0, 0.025, 0.05) + vec3(0.25,0.4,0.45) * cs * 0.18 * lum3(col);
    col = mix(col, wc, clamp(uF.z, 0.0, 1.0));
  }
#endif

#ifdef FX_VHS     // scanlines + desaturated bleed + noise
  {
    float sl = 0.5 + 0.5*sin(uv.y*uRes.y*3.14159);
    col *= 1.0 - 0.16*uH.x*(1.0 - sl);
    col = mix(col, vec3(lum3(col)), 0.2*uH.x);
    col += (hash21(uv*uRes.xy*0.5 + t) - 0.5) * 0.08 * uH.x;
  }
#endif

#ifdef FX_VIGNETTE
  {
    float dv = length((uv - 0.5) * vec2(uRes.x/uRes.y, 1.0));
    col *= 1.0 - smoothstep(uE.y, uE.y + 0.8, dv) * uE.x;
  }
#endif
#ifdef FX_GRAIN
  {
    float gn = hash21(gl_FragCoord.xy + vec2(uK.w*7.13, uK.w*3.71)) - 0.5;
    col += gn * uE.z * 0.12 * (1.0 - abs(lum3(col)*2.0 - 1.0)*0.6);
  }
#endif
#ifdef FX_DITHER   // triangular dither: removes banding in dark gradients and the sky
  {
    HP float n1 = ign(gl_FragCoord.xy + uK.w*vec2(5.588238));
    HP float n2 = ign(gl_FragCoord.yx*1.37 + uK.w*vec2(19.19));
    col += (n1 + n2 - 1.0) * (1.0/255.0);
  }
#endif
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
)GLSL";
