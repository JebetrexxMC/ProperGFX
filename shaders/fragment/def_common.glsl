// TyFMCGFX 1.5.0 deferred screen-space lighting - shared helpers (pasted by "#pragma include", GLSL ES 3.00).
// The game's scene target carries OUR readable depth texture (ps_scene.cpp), so every pixel's world position is rebuilt from it:
//   P = uIVP * ndc  (relative to the camera, double-precision matrix on the CPU - world coordinates reach 3000 m)
// and its normal from the neighbouring depths. Every surface on screen gets the effect without patching the game's shaders.
precision highp float;
precision highp int;
uniform highp sampler2D uDepth;     // scene depth (full size)
uniform highp mat4 uIVP;            // ndc -> world - camera
uniform highp mat4 uVP;             // world - camera -> clip
uniform highp vec4 uCam;            // camera world position, w = time (s)
uniform highp vec4 uSunD;           // direction TOWARD the sun (world, z up), w = sun height factor 0..1 (0 = night)
uniform highp vec4 uRes;            // full size w, h, 1/w, 1/h
uniform highp vec4 uLin;            // projection A, B (view distance = B / (ndc + A)), far clip, frame index
in highp vec2 vUV;

ivec2 gHi;
highp vec3 PosAtUV(highp vec2 uv, out highp float d){
  uv = (floor(uv * uRes.xy + 0.01) + 0.5) * uRes.zw;   // snap to the depth texel centre (+0.01: half-res pixel centres sit exactly on a texel edge)
  d = texture(uDepth, uv).r;
  highp vec4 p = uIVP * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
  return p.xyz / p.w;
}
highp vec3 PosAt(highp vec2 uv){ highp float d; return PosAtUV(uv, d); }
highp float LinDepth(highp float d){ return abs(uLin.y / ((d * 2.0 - 1.0) + uLin.x)); }
// normal from a 1-pixel baseline (texel-snapped), per axis the side with the smaller step (no flipped normals on silhouettes)
highp vec3 NormalAt(highp vec2 uv, highp vec3 C){
  highp vec2 px = uRes.zw;
  uv = (floor(uv * uRes.xy + 0.01) + 0.5) * uRes.zw;     // neighbours at exact texel centres: never the own texel (no zero-length edge)
  highp vec3 R = PosAt(uv + vec2(px.x, 0.0)), L = PosAt(uv - vec2(px.x, 0.0)), U = PosAt(uv + vec2(0.0, px.y)), D = PosAt(uv - vec2(0.0, px.y));
  highp float cl = length(C);
  highp vec3 ex = abs(length(R) - cl) < abs(cl - length(L)) ? R - C : C - L;
  highp vec3 ey = abs(length(U) - cl) < abs(cl - length(D)) ? U - C : C - D;
  highp vec3 N = cross(ex, ey);
  if (!(dot(N, N) > 1e-12)) return vec3(0.0, 0.0, 1.0);
  N = normalize(N);
  if (dot(N, -C) < 0.0) N = -N;
  return N;
}
highp vec3 ToScreen(highp vec3 P){ highp vec4 c = uVP * vec4(P, 1.0); return vec3(c.xy / c.w * 0.5 + 0.5, c.z / c.w * 0.5 + 0.5); }
float IGN(vec2 p){ p += 5.588238 * mod(uLin.w, 64.0); return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }   // interleaved gradient noise
float Hash12(vec2 p){ vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float Noise2(vec2 p){ vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
  return mix(mix(Hash12(i), Hash12(i + vec2(1, 0)), f.x), mix(Hash12(i + vec2(0, 1)), Hash12(i + vec2(1, 1)), f.x), f.y); }
float Fbm(vec2 p){ float v = 0.0, a = 0.5; for (int i = 0; i < 4; ++i) { v += a * Noise2(p); p = p * 2.03 + 17.1; a *= 0.5; } return v; }
float Luma(vec3 c){ return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
