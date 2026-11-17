#version 300 es
// Proper GFX 1.5.0 deferred (screen-space) sun shadows - MASK pass (2 or 4 cascades, PCSS contact hardening). Technique of libZyZyGFX64 (see the GLSL-Samples-source-mod repo):
//  * the game's scene target gets OUR depth texture instead of its depth renderbuffer (ps_scene.cpp), so after RenderScene the
//    depth of every pixel on screen is readable;
//  * each pixel's world position is rebuilt from that depth (inverse view-projection, relative to the camera for precision),
//    its normal from the neighbouring depths;
//  * the position is looked up in two sun shadow-map cascades (hardware 2x2 PCF, 3x3 grid) with normal-offset + slope bias;
//  * the result is a multiply factor (1 = lit) that fragment/shadow_apply.glsl multiplies into the scene colour.
// Every surface on screen receives - buildings, roads, terrain, vehicles AND pedestrians - without patching the game's shaders.
precision highp float;
precision highp int;
uniform highp sampler2D uDepth;          // unit 0: scene depth (full size)
uniform highp sampler2DShadow uMap0;     // unit 1: near cascade (compare sampler)
uniform highp sampler2DShadow uMap1;     // unit 2: 2nd cascade
uniform highp sampler2DShadow uMap2;     // unit 3: 3rd cascade (4-cascade mode)
uniform highp sampler2DShadow uMap3;     // unit 4: far cascade (4-cascade mode)
uniform highp sampler2D uRaw0;           // unit 5: near cascade, raw depth (sampler object without compare) - PCSS blocker search
uniform highp sampler2D uRaw1;           // unit 6: 2nd cascade, raw depth
uniform highp mat4 uIVP;                 // ndc -> world - camera
uniform highp mat4 uL0;                  // (world - camera) -> light clip of each cascade
uniform highp mat4 uL1;
uniform highp mat4 uL2;
uniform highp mat4 uL3;
uniform highp vec4 uTexB;                // texel size in metres of cascade 2, 3; z cascade count (2 / 4)
uniform highp vec4 uPcss;                // x on, y penumbra growth per metre of blocker distance, z light depth range (m)
uniform highp vec4 uP;                   // x mask->depth pixel scale, y camera height above the sea (world z, 0 = unknown), z softness, w bias multiplier
uniform highp vec4 uSun;                 // xyz direction TOWARD the sun, w light depth units per metre
uniform highp vec4 uTex;                 // texel size in metres near, far; 1 / map size near, far
uniform highp vec2 uFog;                 // the game's distance fog: start (m), 1 / (end - start); 0,0 = unknown
uniform mediump vec4 uShade;             // x strength (0..1, sun fade + timecyc applied), y tint amount, z far-cascade fade start (0..1 of its square)
uniform mediump vec3 uTint;              // colour of the shadowed part (Tint R/G/B)
// SAShaderL-compatible globals (set by Proper GFX like for every other .glsl)
uniform int ShaderFlags;                 // 0x20000000 | 0x100 = deferred shadow mask pass (set, not needed by this shader)
uniform vec3 SunVector;
uniform float UnderWaterness;
out vec4 oColor;

ivec2 gHi;
vec2 gInv;
highp vec3 WorldAt(ivec2 q){
  q = clamp(q, ivec2(0), gHi);
  highp float d = texelFetch(uDepth, q, 0).r;
  highp vec4 p = uIVP * vec4((vec2(q) + 0.5) * gInv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
  return p.xyz / p.w;
}
// PCSS blocker search (cascades 0 / 1 only): average depth of the occluders around the pixel -> penumbra width.
// Far from its caster a shadow gets softer (a lamp post's shadow tip), right at the contact it stays sharp.
float Penumbra(highp sampler2D raw, highp vec3 sc, float bias, float texelM, float inv){
  float sum = 0.0, n = 0.0;
  for (int i = 0; i < 8; ++i) {
    float a = float(i) * 0.785398 + 0.39;
    highp vec2 o = vec2(cos(a), sin(a)) * (float(i & 1) + 1.5) * 2.5 * inv;
    highp float z = texture(raw, sc.xy + o).r;
    if (z < sc.z - bias) { sum += z; n += 1.0; }
  }
  if (n < 0.5) return 1.0;
  highp float dm = (sc.z - sum / n) * uPcss.z;                      // metres between receiver and blockers
  return clamp(dm * uPcss.y / max(texelM, 0.01), 0.6, 5.0);         // filter radius multiplier (texels)
}
// one cascade: 3x3 grid of hardware PCF taps, centre weighted (= smooth ~4-texel edge, the same every frame - no noise)
float CascadeP(highp sampler2DShadow m, highp mat4 L, highp vec3 C, highp vec3 N, float texelM, float inv, float ndl, out float edge, int rawIdx){
  float tanA = sqrt(max(1.0 - ndl * ndl, 0.0)) / max(ndl, 0.1);                  // slope: low sun on a wall = more bias
  highp vec3 P = C + N * (texelM * 1.2 * uP.w) + uSun.xyz * 0.02;                // normal offset (camera independent)
  float bias = (0.03 + texelM * 1.5 * min(tanA, 10.0)) * uP.w * uSun.w;           // metres -> light depth
  highp vec4 lp = L * vec4(P, 1.0);
  highp vec3 sc = lp.xyz / lp.w * 0.5 + 0.5;
  edge = max(abs(sc.x - 0.5), abs(sc.y - 0.5)) * 2.0;
  if (edge >= 1.0 || sc.z >= 1.0) { edge = 2.0; return 1.0; }
  float k = 1.0;
  if (uPcss.x > 0.5) {
    if (rawIdx == 0) k = Penumbra(uRaw0, sc, bias, texelM, inv);
    else if (rawIdx == 1) k = Penumbra(uRaw1, sc, bias, texelM, inv);
  }
  float r = 1.5 * uP.z * inv * k, v = 0.0;
  for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x)
    v += texture(m, vec3(sc.xy + vec2(float(x), float(y)) * r, sc.z - bias)) * ((x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0));
  return v * (1.0 / 16.0);
}
void main(){
  gHi = textureSize(uDepth, 0) - 1;
  gInv = 1.0 / vec2(gHi + 1);
  ivec2 ip = ivec2(gl_FragCoord.xy * uP.x);
  highp float d = texelFetch(uDepth, clamp(ip, ivec2(0), gHi), 0).r;
  oColor = vec4(1.0);
  if (d >= 0.99999 || uShade.x <= 0.001) return;                                   // sky / off
  highp vec3 C = WorldAt(ip);
  // water writes no depth in SA: below the sea surface the depth is the SEA FLOOR. Move the point up the view ray to the
  // surface (z = 0) so piers / palms do not cast onto the floor through the water; half strength on water.
  bool water = uP.y > 0.0 && C.z + uP.y < -0.5;                                  // > 0.5 m below sea level (beaches / docks at z ~ 0 are land)
  highp vec3 N = vec3(0.0, 0.0, 1.0);
  if (water) C *= uP.y / -C.z;
  else {
    // normal from a 2-pixel baseline, per axis the side with the smaller depth step (no flipped normals on silhouettes)
    highp vec3 R = WorldAt(ip + ivec2(2, 0)), Lf = WorldAt(ip - ivec2(2, 0)), U = WorldAt(ip + ivec2(0, 2)), D = WorldAt(ip - ivec2(0, 2));
    highp float cl = length(C);
    highp vec3 ex = abs(length(R) - cl) < abs(cl - length(Lf)) ? R - C : C - Lf;
    highp vec3 ey = abs(length(U) - cl) < abs(cl - length(D)) ? U - C : C - D;
    N = normalize(cross(ex, ey));
    if (!(dot(N, N) > 0.5)) return;
    if (dot(N, -C) < 0.0) N = -N;
  }
  float ndl = dot(N, uSun.xyz);
  float dist = length(C);
  // cascades from near to far; each one blends into the next over its outer 20 %, the last one fades out
  float e0 = 0.0, e1 = 2.0, e2 = 2.0, e3 = 2.0, eLast = 2.0;
  bool four = uTexB.z > 3.0;
  float v = CascadeP(uMap0, uL0, C, N, uTex.x, uTex.z, ndl, e0, 0);
  if (e0 > 0.8) {
    float v1 = CascadeP(uMap1, uL1, C, N, uTex.y, uTex.w, ndl, e1, 1);
    eLast = e1;
    if (four && e1 > 0.8) {
      float v2 = CascadeP(uMap2, uL2, C, N, uTexB.x, uTex.w, ndl, e2, 2);
      eLast = e2;
      if (e2 > 0.8) {
        float v3 = CascadeP(uMap3, uL3, C, N, uTexB.y, uTex.w, ndl, e3, 3);
        eLast = e3;
        v3 = mix(v3, 1.0, smoothstep(uShade.z, 1.0, e3));
        v2 = e2 >= 1.0 ? v3 : mix(v2, v3, smoothstep(0.8, 1.0, e2));
      }
      v1 = e1 >= 1.0 ? v2 : mix(v1, v2, smoothstep(0.8, 1.0, e1));
    } else v1 = mix(v1, 1.0, smoothstep(uShade.z, 1.0, e1));                      // far border: fade out
    v = e0 >= 1.0 ? v1 : mix(v, v1, smoothstep(0.8, 1.0, e0));
  }
  // a surface that faces away from the sun IS in shadow (single-sided facades have no back side in the map),
  // only where the cascades reach; back faces at half strength (the game's lighting already darkened them)
  float cov = e0 < 1.0 ? 1.0 : (eLast >= 1.0 ? 0.0 : 1.0 - smoothstep(uShade.z, 1.0, eLast));
  // 1.4.8: only with the sun reasonably high. With a low sun (morning / evening) every gentle slope turned away from it went
  // fully dark - whole hillsides and roads in shadow with no object casting it. Low sun: the map's real shadows only.
  v = mix(v, 0.0, (1.0 - smoothstep(-0.25, 0.0, ndl)) * cov * smoothstep(0.15, 0.35, uSun.z));
  v = mix(1.0, v, mix(0.5, 1.0, smoothstep(-0.25, 0.25, ndl)));
  if (water) v = mix(1.0, v, 0.5);
  if (uFog.y > 0.0) v = mix(v, 1.0, clamp((dist - uFog.x) * uFog.y, 0.0, 1.0)); // the game already fogged this pixel
  v = mix(v, 1.0, clamp(UnderWaterness, 0.0, 1.0));                                // camera under water: no sun shadows
  float shade = (1.0 - v) * uShade.x;
  vec3 shadowCol = uTint * uShade.y;                                                // tint 0 = plain darkening
  oColor = vec4(mix(vec3(1.0), shadowCol, shade), 1.0);
}
