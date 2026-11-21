#version 300 es
// TyFMCGFX 1.5.0 [TAA] temporal anti-aliasing: each pixel is reprojected into the previous frame with the scene depth and both
// cameras (no motion vectors needed for the static world), the history is clamped to the 3x3 neighbourhood of the current frame
// (no ghosting) and blended. GTA SA's projection cannot be jittered, so this smooths shimmering / crawling edges rather than resolving
// sub-pixel detail (that is FXAA / SMAA's job).
#pragma include "def_common.glsl"
uniform mediump sampler2D uCur;     // unit 1: this frame (post FX)
uniform mediump sampler2D uHist;    // unit 2: history
uniform highp mat4 uReproj;         // ndc now -> clip of the previous frame
uniform highp vec4 uP;              // x blend (history weight), y reset
out vec4 oColor;
void main(){
  vec4 c = texture(uCur, vUV);
  if (uP.y > 0.5) { oColor = c; return; }
  float d = texture(uDepth, vUV).r;
  highp vec4 pc = uReproj * vec4(vUV * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
  vec2 puv = pc.xy / pc.w * 0.5 + 0.5;
  if (d >= 0.99999) puv = vUV;                                              // sky: no parallax worth reprojecting
  vec3 mn = c.rgb, mx = c.rgb, m1 = c.rgb, m2 = c.rgb * c.rgb;
  for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
    if (x == 0 && y == 0) continue;
    vec3 s = texture(uCur, vUV + vec2(float(x), float(y)) * uRes.zw).rgb;
    mn = min(mn, s); mx = max(mx, s); m1 += s; m2 += s * s;
  }
  m1 /= 9.0; vec3 sd = sqrt(max(m2 / 9.0 - m1 * m1, 0.0));
  mn = max(mn, m1 - sd * 1.25); mx = min(mx, m1 + sd * 1.25);
  vec3 h = texture(uHist, puv).rgb;
  h = clamp(h, mn, mx);
  float w = uP.x;
  if (puv.x < 0.0 || puv.y < 0.0 || puv.x > 1.0 || puv.y > 1.0) w = 0.0;
  w *= 1.0 - clamp(length((puv - vUV) * uRes.xy) * 0.02, 0.0, 0.6);       // fast motion: trust the current frame more
  oColor = vec4(mix(c.rgb, h, w), c.a);
}
