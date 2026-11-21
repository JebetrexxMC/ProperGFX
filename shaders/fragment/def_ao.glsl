#version 300 es
// TyFMCGFX 1.5.0 [AmbientOcclusion] - SSAO / HBAO / SSGI from the scene depth (half resolution by default).
//   SSAO : 4 directions x 3 steps, averaged occlusion (soft, cheap)
//   HBAO : 6 directions x 4 steps, horizon based (the highest occluder per direction counts = crisper creases)
//   SSGI : HBAO + one bounce of the colour of the occluders (colour bleeding: a red wall tints the floor next to it)
// Output: rgb = indirect light (GI), a = ambient visibility (1 = open). Blurred by def_bilateral.glsl, used by def_composite.glsl.
#pragma include "def_common.glsl"
uniform mediump sampler2D uScene;   // unit 1: scene colour copy
uniform highp vec4 uP;              // x radius (m), y intensity, z mode (0 SSAO, 1 HBAO, 2 SSGI), w projection scale (proj[5] * 0.5)
uniform highp vec4 uQ;              // 1.6.0 x GIQuality 0 / 1 / 2 (SSGI / HBAO: 4 / 6 / 8 directions)
out vec4 oColor;
void main(){
  highp float d;
  highp vec3 C = PosAtUV(vUV, d);
  if (d >= 0.99999) { oColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
  highp vec3 N = NormalAt(vUV, C);
  float dist = length(C);
  float R = uP.x * (1.0 + dist * 0.004);                                   // a little wider far away (no "outline" look)
  float rs = clamp(R * uP.w / max(LinDepth(d), 0.1), uRes.w * 3.0, 0.12);  // screen radius (uv)
  int mode = int(uP.z + 0.5);
  int q = int(uQ.x + 0.5);
  int dirs = mode == 0 ? 4 : (q == 0 ? 4 : (q == 1 ? 6 : 8)), steps = mode == 0 ? 3 : (q == 2 ? 5 : 4);
  float rnd = Hash12(gl_FragCoord.xy * 1.37 + uLin.w * 7.0) * 6.2831853, jit = IGN(gl_FragCoord.xy);   // white-noise rotation: IGN is near-periodic along x (stripes on walls)
  float occ = 0.0; vec3 gi = vec3(0.0);
  vec2 asp = vec2(uRes.y * uRes.z, 1.0);                                   // round in pixels
  for (int i = 0; i < 8; ++i) {
    if (i >= dirs) break;
    float a = rnd + float(i) * (6.2831853 / float(dirs));
    vec2 dir = vec2(cos(a), sin(a)) * asp;
    float hor = 0.0, sum = 0.0;
    for (int j = 0; j < 5; ++j) {
      if (j >= steps) break;
      float t = (float(j) + jit) / float(steps);
      vec2 uv = vUV + dir * rs * t * t;                                    // denser close to the pixel
      if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) break;
      highp vec3 S = PosAt(uv);
      highp vec3 V = S - C; float l = length(V);
      if (l < max(1e-3, dist * 0.004)) continue;              // depth-precision noise on the own plane is not an occluder
      float fall = clamp(1.0 - (l * l) / (R * R), 0.0, 1.0);
      float hgt = dot(N, V) - max(0.03, dist * 0.006);                    // height above the own plane minus the depth-quantisation step (no stripes on grazing walls)
      float h = max(hgt / l - 0.15, 0.0) * fall;
      hor = max(hor, h); sum += h;
      if (mode == 2 && h > 0.0) gi += texture(uScene, uv).rgb * h;
    }
    occ += mode == 0 ? sum / float(steps) : hor;
  }
  occ /= float(dirs);
  float vis = clamp(1.0 - occ * uP.y * 1.6, 0.0, 1.0);
  if (mode == 2) gi /= float(dirs * steps); 
  oColor = vec4(gi, vis);
}
