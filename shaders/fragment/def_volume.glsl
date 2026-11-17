#version 300 es
// Proper GFX 1.5.0 [VolumetricFog] height fog with god rays + [VolumetricHeadlights] (experimental), ray-marched per pixel
// (half resolution, 12 steps, dithered start). The sun's visibility along the ray is read from the dynamic-shadow cascade, so light shafts
// appear between buildings / trees; without dynamic shadows the fog is still lit by the sun (forward scattering) but has no shafts.
// Output: rgb = in-scattered light, a = transmittance (scene * a + rgb).
#pragma include "def_common.glsl"
uniform highp sampler2DShadow uMap; // unit 2: a sun shadow cascade
uniform highp mat4 uL;              // world - camera -> that cascade's light clip
uniform highp vec4 uV;              // x density, y height falloff, z god rays, w shadow map valid
uniform highp vec4 uSunC;           // sun colour (rgb), w max march distance (m)
uniform highp vec4 uAmb;            // ambient fog colour (rgb), w headlight strength
uniform highp vec4 uHL0;            // left headlight (world - camera), w on
uniform highp vec4 uHL1;            // right headlight
uniform highp vec4 uHLD;            // headlight direction (world)
out vec4 oColor;
void main(){
  highp float d;
  highp vec3 C = PosAtUV(vUV, d);
  highp vec3 dir = normalize(C);
  float L = d >= 0.99999 ? uSunC.w : min(length(C), uSunC.w);
  const int N = 12;
  float dt = L / float(N);
  float t = dt * IGN(gl_FragCoord.xy);
  float mu = dot(dir, uSunD.xyz);
  float g = 0.55;
  float phase = (1.0 - g * g) / (12.566 * pow(1.0 + g * g - 2.0 * g * mu, 1.5)) * 6.0 + 0.25;   // Henyey-Greenstein, forward scattering
  vec3 ins = vec3(0.0); float T = 1.0;
  for (int i = 0; i < N; ++i) {
    highp vec3 X = dir * t;
    float h = X.z + uCam.z;
    float dens = 0.0025 * uV.x * exp(-max(h, 0.0) * uV.y * 0.03);
    float vis = 1.0;
    if (uV.w > 0.5 && uV.z > 0.0) {
      highp vec4 lp = uL * vec4(X, 1.0);
      highp vec3 sc = lp.xyz / lp.w * 0.5 + 0.5;
      if (all(greaterThan(sc.xy, vec2(0.0))) && all(lessThan(sc.xy, vec2(1.0))) && sc.z < 1.0)
        vis = mix(1.0, texture(uMap, vec3(sc.xy, sc.z - 0.0015)), clamp(uV.z, 0.0, 1.0));
    }
    vec3 Li = uSunC.rgb * (phase * vis * uSunD.w) + uAmb.rgb;
    if (uHL0.w > 0.5) {
      for (int k = 0; k < 2; ++k) {
        highp vec3 lv = X - (k == 0 ? uHL0.xyz : uHL1.xyz);
        float dl = length(lv);
        float cone = smoothstep(0.86, 0.975, dot(lv / max(dl, 1e-3), uHLD.xyz));
        Li += vec3(1.0, 0.93, 0.8) * cone * uAmb.w * 6.0 / (1.0 + dl * dl * 0.03) * step(dl, 45.0);
      }
    }
    float ex = exp(-dens * dt);
    ins += T * (1.0 - ex) * Li;
    T *= ex;
    t += dt;
  }
  oColor = vec4(ins, T);
}
