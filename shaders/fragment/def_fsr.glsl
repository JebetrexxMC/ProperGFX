#version 300 es
// TyFMCGFX 1.5.0 [FSR] AMD FidelityFX Super Resolution 1 - RCAS (robust contrast-adaptive sharpening), the second half of FSR 1,
// with its noise-aware lobe limit. GTA SA's internal resolution is set by the game's own Resolution option (lower it and FSR restores
// the crispness); the EASU upscale itself needs the game to render into a smaller target, which the mobile renderer does not expose.
precision mediump float;
uniform mediump sampler2D uCur;     // unit 1
uniform highp vec4 uRes;
uniform highp vec4 uP;              // x sharpness 0..1
in highp vec2 vUV;
out vec4 oColor;
void main(){
  vec2 px = uRes.zw;
  vec3 b = texture(uCur, vUV + vec2(0.0, -px.y)).rgb, d = texture(uCur, vUV + vec2(-px.x, 0.0)).rgb;
  vec4 e4 = texture(uCur, vUV); vec3 e = e4.rgb;
  vec3 f = texture(uCur, vUV + vec2(px.x, 0.0)).rgb, h = texture(uCur, vUV + vec2(0.0, px.y)).rgb;
  vec3 mn = min(min(b, d), min(f, h)), mx = max(max(b, d), max(f, h));
  vec3 hitMin = mn / (4.0 * mx + 1e-4), hitMax = (1.0 - mx) / (4.0 * mn - 4.0 - 1e-4);
  vec3 lobeRGB = max(-hitMin, hitMax);
  float lobe = max(-0.1875, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * exp2(-(1.0 - uP.x) * 2.0);
  float bL = dot(b, vec3(0.5, 1.0, 0.5)), dL = dot(d, vec3(0.5, 1.0, 0.5)), eL = dot(e, vec3(0.5, 1.0, 0.5));
  float fL = dot(f, vec3(0.5, 1.0, 0.5)), hL = dot(h, vec3(0.5, 1.0, 0.5));
  float nz = abs(0.25 * (bL + dL + fL + hL) - eL) / max(max(max(bL, dL), max(eL, max(fL, hL))) - min(min(bL, dL), min(eL, min(fL, hL))), 1e-4);
  lobe *= 1.0 - 0.5 * clamp(nz, 0.0, 1.0);                                  // noise removal
  vec3 c = (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);
  oColor = vec4(clamp(c, 0.0, 1.0), e4.a);
}
