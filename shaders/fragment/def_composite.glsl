#version 300 es
// Proper GFX 1.5.0 deferred COMPOSITE - one full-screen pass over the game's scene target right after RenderScene (3D world only:
// the HUD, radar, menus and the pause menu are drawn later and are never touched). Per pixel, from the scene depth:
//   [AmbientOcclusion] AO + SSGI bounce   [Skylight] sky occlusion of the sky ambient   [AmbientLighting] tricoloured ambient
//   [ContactShadows] short screen-space rays toward the sun     [SSR] screen-space reflections (wet roads, puddles, flat shiny floors)
//   [WorldRain] puddles + animated rain ripples while CWeather::WetRoads > 0     [WaterCaustics] on the sea floor and under water
//   sky pixels: [PBRSky] Rayleigh / Mie sky blend, [VolumetricClouds], [Aurora]     then [VolumetricFog] and [RainFall] streaks
#pragma include "def_common.glsl"
uniform mediump sampler2D uScene;   // unit 1: scene colour copy
uniform mediump sampler2D uAO;      // unit 2: AO (a) + GI (rgb), blurred
uniform mediump sampler2D uVol;     // unit 3: volumetric fog (rgb in-scatter, a transmittance)
uniform highp vec4 uAmbS;           // sky ambient colour, w tricolour strength
uniform highp vec4 uAmbH;           // horizon ambient colour, w skylight strength
uniform highp vec4 uAmbG;           // ground ambient colour, w AO on (intensity)
uniform highp vec4 uSunC;           // sun colour, w contact-shadow strength
uniform highp vec4 uA;              // x GI strength, y contact length (m), z SSR strength, w SSR on dry flat surfaces
uniform highp vec4 uW;              // x wetness (roads), y puddles, z ripples, w caustics strength
uniform highp vec4 uS;              // x PBR sky blend, y cloud coverage, z cloud density, w aurora strength
uniform highp vec4 uS2;             // x cloud speed, y night 0..1, z rain fall streaks, w underwater 0..1
uniform highp vec4 uOn;             // x volumetric fog on, y SSR steps, z AO buffer valid, w rain amount (CWeather::Rain)
out vec4 oColor;

vec3 Ambient(vec3 n){ return n.z >= 0.0 ? mix(uAmbH.rgb, uAmbS.rgb, n.z) : mix(uAmbH.rgb, uAmbG.rgb, -n.z); }

// cheap analytic Rayleigh + Mie single scattering sky (Preetham-like), sun direction from the game's time cycle
vec3 PbrSky(vec3 v){
  vec3 s = uSunD.xyz;
  float mu = dot(v, s), vz = max(v.z, 0.0);
  float zen = 1.0 / (vz + 0.15 * pow(93.885 - degrees(acos(clamp(vz, 0.0, 1.0))), -1.253));    // optical depth
  vec3 betaR = vec3(5.8, 13.5, 33.1) * 0.06, betaM = vec3(0.21);
  vec3 ext = exp(-(betaR + betaM) * zen * 0.6);
  float pR = 0.0596831 * (1.0 + mu * mu);
  float g = 0.76, pM = 0.1193662 * ((1.0 - g * g) * (1.0 + mu * mu)) / ((2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * mu, 1.5));
  float sunE = 22.0 * clamp(1.0 - exp(-((s.z + 0.08) / 0.12)), 0.0, 1.0);
  vec3 Lin = sunE * (betaR * pR + betaM * pM) / (betaR + betaM) * (1.0 - ext);
  Lin *= mix(vec3(1.0), pow(sunE * (betaR * pR + betaM * pM) / (betaR + betaM) * ext, vec3(0.5)), clamp(pow(1.0 - s.z, 5.0), 0.0, 1.0));
  vec3 night = vec3(0.012, 0.018, 0.04) * (0.6 + vz);
  return max(Lin * 0.06, vec3(0.0)) + night;
}

// volumetric cloud slab 650..1050 m, 6 steps + 2 light steps toward the sun (beer-powder lighting)
float CloudD(vec3 p){
  vec2 q = p.xy * 0.0011 + vec2(uCam.w * 0.004 * uS2.x, uCam.w * 0.0015 * uS2.x);
  float base = Fbm(q) * 0.75 + Fbm(q * 3.1 + 7.0) * 0.25;
  float hfrac = clamp((p.z - 650.0) / 400.0, 0.0, 1.0);
  float shape = smoothstep(0.0, 0.2, hfrac) * smoothstep(1.0, 0.55, hfrac);
  return clamp((base - (1.0 - uS.y)) * 2.2, 0.0, 1.0) * shape * uS.z;
}
vec4 Clouds(vec3 v){
  if (v.z < 0.02) return vec4(0.0, 0.0, 0.0, 1.0);
  float camZ = uCam.z;
  float t0 = (650.0 - camZ) / v.z, t1 = (1050.0 - camZ) / v.z;
  if (t1 <= 0.0) return vec4(0.0, 0.0, 0.0, 1.0);
  t0 = max(t0, 0.0);
  float dt = (t1 - t0) / 6.0, t = t0 + dt * IGN(gl_FragCoord.xy);
  vec3 col = vec3(0.0); float T = 1.0;
  vec3 sunL = uSunC.rgb * 1.4 + Ambient(vec3(0.0, 0.0, 1.0)) * 0.6;
  for (int i = 0; i < 6; ++i) {
    vec3 p = uCam.xyz + v * t;
    float dn = CloudD(p);
    if (dn > 0.01) {
      float ls = CloudD(p + uSunD.xyz * 60.0) + CloudD(p + uSunD.xyz * 150.0);
      float beer = exp(-ls * 1.6), powder = 1.0 - exp(-dn * 4.0);
      vec3 c = mix(Ambient(vec3(0.0, 0.0, 1.0)) * 0.8, sunL, beer * mix(1.0, powder, 0.5));
      float a = 1.0 - exp(-dn * dt * 0.012);
      col += T * a * c; T *= 1.0 - a;
      if (T < 0.03) break;
    }
    t += dt;
  }
  float fade = smoothstep(0.02, 0.12, v.z);                                 // thin out toward the horizon
  return vec4(col * fade, mix(1.0, T, fade));
}

vec3 Aurora(vec3 v){
  if (v.z < 0.03) return vec3(0.0);
  vec2 p = v.xy / (v.z + 0.25) * 2.2;
  vec3 acc = vec3(0.0);
  for (int i = 0; i < 4; ++i) {
    float fi = float(i);
    vec2 q = p * (1.0 + fi * 0.18) + vec2(uCam.w * 0.012, fi * 1.7);
    float band = Fbm(vec2(q.x * 0.6 + Fbm(q * 0.5 + uCam.w * 0.02) * 2.0, q.y * 0.15));
    float curtain = pow(smoothstep(0.45, 0.75, band), 2.0) * (1.0 - fi * 0.2);
    acc += mix(vec3(0.1, 1.0, 0.45), vec3(0.6, 0.2, 1.0), fi / 3.0) * curtain;
  }
  return acc * smoothstep(0.03, 0.25, v.z) * 0.18;
}

float Caustic(vec2 p, float t){
  vec2 q = p * 0.45; float c = 0.0;
  for (int i = 0; i < 3; ++i) {
    q += vec2(sin(q.y * 1.7 + t * 0.9), cos(q.x * 1.3 - t * 0.7)) * 0.55;
    c += 1.0 / (1.0 + 18.0 * abs(sin(q.x + q.y) * cos(q.x - q.y)));
  }
  return pow(c / 3.0, 2.2) * 2.5;
}

// rain ripples: one ring per cell, random phase, expanding and fading (normal perturbation)
vec2 Ripples(vec2 p, float t){
  vec2 g = vec2(0.0);
  for (int l = 0; l < 2; ++l) {
    vec2 q = p * (l == 0 ? 2.2 : 3.7) + float(l) * 11.3;
    vec2 i = floor(q), f = fract(q) - 0.5;
    float h = Hash12(i + float(l) * 7.0);
    vec2 o = (vec2(Hash12(i + 3.1), Hash12(i + 5.7)) - 0.5) * 0.5;
    float ph = fract(t * (0.9 + h * 0.6) + h);
    vec2 dv = f - o; float r = length(dv);
    float ring = sin((r - ph * 0.45) * 60.0) * smoothstep(0.08, 0.0, abs(r - ph * 0.45)) * (1.0 - ph);
    g += dv / max(r, 1e-3) * ring;
  }
  return g * 0.35;
}

vec3 TraceSSR(vec3 C, vec3 R, float dist, out float hit){
  hit = 0.0;
  float stepL = 0.25 + dist * 0.015;
  float t = stepL * (0.5 + IGN(gl_FragCoord.xy));                          // jittered start: no stair-step bands in the reflection
  float tPrev = 0.0;
  int n = int(uOn.y);
  for (int i = 0; i < 24; ++i) {
    if (i >= n) break;
    vec3 P = C + R * t;
    vec3 s = ToScreen(P);
    if (s.x < 0.0 || s.y < 0.0 || s.x > 1.0 || s.y > 1.0 || s.z >= 1.0) break;
    float sd = LinDepth(texture(uDepth, s.xy).r), rd = LinDepth(s.z);
    float th = 0.35 + t * 0.08;
    if (rd > sd + 0.03 && rd - sd < th) {
      float t0 = tPrev, t1 = t;                                             // bisect between the last miss and the hit
      for (int k = 0; k < 4; ++k) {
        float tm = (t0 + t1) * 0.5;
        vec3 sm = ToScreen(C + R * tm);
        if (LinDepth(sm.z) > LinDepth(texture(uDepth, sm.xy).r)) t1 = tm; else t0 = tm;
      }
      vec2 b = ToScreen(C + R * t1).xy;
      vec2 e = smoothstep(0.0, 0.08, b) * smoothstep(1.0, 0.92, b);
      hit = e.x * e.y * (1.0 - float(i) / float(n));
      return texture(uScene, b).rgb;
    }
    tPrev = t;
    stepL *= 1.3; t += stepL;
  }
  return vec3(0.0);
}

void main(){
  vec4 sc = texture(uScene, vUV);
  vec3 col = sc.rgb;
  highp float d;
  highp vec3 C = PosAtUV(vUV, d);
  vec3 V = normalize(C);
  float sunH = uSunD.w;
  if (d < 0.99999) {
    vec3 N = NormalAt(vUV, C);
    float dist = length(C);
    vec3 W = C + uCam.xyz;
    // --- ambient occlusion / GI / skylight / tricolour ambient
    vec4 ao = uOn.z > 0.5 ? texture(uAO, vUV) : vec4(0.0, 0.0, 0.0, 1.0);
    float vis = mix(1.0, ao.a, uAmbG.w);
    vec3 amb = Ambient(N);
    float sky = clamp(N.z * 0.5 + 0.5, 0.0, 1.0) * mix(1.0, ao.a * ao.a, uAmbH.w);
    col *= vis;
    col += col * (amb - vec3(Luma(amb))) * uAmbS.w * 1.2;                   // tint by sky / horizon / ground
    col += col * amb * (sky - 0.5) * uAmbH.w * 0.5;                         // skylight: open surfaces get more sky light, covered ones less
    col += ao.rgb * uA.x * (0.35 + col);
    // --- contact shadows
    float ndl = dot(N, uSunD.xyz);
    if (uSunC.w > 0.0 && sunH > 0.01 && ndl > 0.0 && dist < 70.0) {
      float occ = 0.0, j = IGN(gl_FragCoord.xy);
      vec3 P0 = C + N * (0.03 + dist * 0.002);
      for (int i = 0; i < 8; ++i) {
        float t = (float(i) + j) / 8.0 * uA.y;
        vec3 s = ToScreen(P0 + uSunD.xyz * t);
        if (s.x < 0.0 || s.y < 0.0 || s.x > 1.0 || s.y > 1.0) break;
        float sd = LinDepth(texture(uDepth, s.xy).r), rd = LinDepth(s.z);
        float dz = rd - sd;
        if (dz > 0.02 + dist * 0.001 && dz < 0.6) { occ = 1.0 - float(i) / 8.0; break; }
      }
      col *= 1.0 - occ * uSunC.w * sunH * smoothstep(70.0, 40.0, dist) * 0.6;
    }
    // --- wet world: puddles, ripples, darkening
    float up = smoothstep(0.78, 0.96, N.z);
    float wet = uW.x * up;
    float puddle = smoothstep(0.48, 0.62, Fbm(W.xy * 0.12)) * wet * uW.y;
    vec3 Nr = N;
    if (wet > 0.01 && uW.z > 0.0 && dist < 60.0) {
      vec2 rp = Ripples(W.xy, uCam.w) * uW.z * min(uOn.w * 1.5 + 0.2, 1.0);
      Nr = normalize(N + vec3(rp, 0.0) * (0.3 + puddle));
    }
    col *= 1.0 - wet * 0.25 - puddle * 0.2;
    // --- screen-space reflections
    float cosV = clamp(dot(-V, Nr), 0.0, 1.0);
    float fres = 0.04 + 0.96 * pow(1.0 - cosV, 5.0);
    float refl = max(uA.w * smoothstep(0.93, 0.99, N.z) * 0.35, wet * 0.35 + puddle * 0.65) * uA.z;
    refl *= mix(fres, 1.0, puddle * 0.5) * 2.2;
    if (refl > 0.01 && dist < 160.0) {
      vec3 R = reflect(V, Nr);
      if (dot(R, N) < 0.02) R = reflect(V, N);              // rippled normal pointing into the surface: no false hits / dark streaks
      float hit;
      vec3 rc = TraceSSR(C, R, dist, hit);
      vec3 env = Ambient(R) * (0.6 + sunH);
      env *= clamp(Luma(col) * 2.0, 0.25, 1.0);              // misses keep the pixel's own shading (puddles must not erase shadows)
      col = mix(col, mix(env, rc, hit), clamp(refl * mix(0.5, 1.0, hit), 0.0, 0.85));
    }
    // --- caustics on the sea floor (water writes no depth: under the sea surface the depth is the floor)
    if (uW.w > 0.0 && (W.z < -0.3 || uS2.w > 0.5)) {
      float depthB = max(-W.z, 0.0);
      col += col * Caustic(W.xy, uCam.w) * uW.w * (0.3 + sunH) * exp(-depthB * 0.12) * 0.5;
    }
  } else {
    // --- sky
    if (uS.x > 0.0) { vec3 ps = PbrSky(V); col = mix(col, ps, uS.x * smoothstep(-0.02, 0.06, V.z)); }
    if (uS.z > 0.0 && uS.y > 0.0) { vec4 cl = Clouds(V); col = col * cl.a + cl.rgb; }
    if (uS.w > 0.0 && uS2.y > 0.05) col += Aurora(V) * uS.w * uS2.y;
  }
  // --- volumetric fog (half-res buffer)
  if (uOn.x > 0.5) { vec4 vf = texture(uVol, vUV); col = col * vf.a + vf.rgb; }
  // --- falling rain streaks
  if (uS2.z > 0.0 && uOn.w > 0.01 && uS2.w < 0.5) {
    float r = 0.0;
    for (int l = 0; l < 3; ++l) {
      float fl = float(l);
      vec2 q = vec2(vUV.x * (60.0 + fl * 35.0) * uRes.x * uRes.w, vUV.y * (1.5 + fl) + uCam.w * (2.2 + fl * 0.8));
      q.x += vUV.y * 3.0;                                                   // a little slant
      vec2 i = floor(q); vec2 f = fract(q);
      float h = Hash12(i + fl * 13.0);
      float line = smoothstep(0.06, 0.0, abs(f.x - 0.5 - (h - 0.5) * 0.6)) * smoothstep(0.0, 0.3, f.y) * smoothstep(1.0, 0.6, f.y);
      r += line * step(0.86 - uOn.w * 0.25, h) * (1.0 - fl * 0.25);
    }
    col = mix(col, vec3(0.75, 0.8, 0.85) * (0.3 + Luma(col)), clamp(r * uS2.z * 0.25, 0.0, 0.4));
  }
  oColor = vec4(col, sc.a);
}
