#version 300 es
// TyFMCGFX 1.6.0 deferred COMPOSITE - one full-screen pass over the game's scene target right after RenderScene (3D world only:
// the HUD, radar, menus and the pause menu are drawn later and are never touched). Per pixel, from the scene depth:
//   [AmbientOcclusion] AO + SSGI bounce   [Skylight] sky occlusion of the sky ambient   [AmbientLighting] tricoloured ambient
//   [ContactShadows] short screen-space rays toward the sun     [SSR] screen-space reflections (wet roads, puddles, flat shiny floors)
//   [WorldRain] puddles + animated rain ripples while CWeather::WetRoads > 0     [WaterCaustics] on the sea floor and under water
//   sky pixels: [PBRSky] Rayleigh / Mie sky blend, [VolumetricClouds], [Aurora]     then [VolumetricFog] and [RainFall] streaks
//   1.6.0 (ZyZGfx 2.0 features): [DynamicLights] lamps / neon / car lights shade the world (+ screen-space shadows, fog glow),
//   [LensGlare], [WetLightStreaks], [Moon] light / glitter, [NightLook], [NightSky] city glow / stars / Milky Way / shooting stars,
//   AO multi-bounce, lightning flashes
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
uniform highp vec4 uLP[32];         // dynamic lights: xyz position (world - camera), w radius (m)
uniform highp vec4 uLC[32];         // rgb colour * intensity, w kind (0 lamp, 1 car, 2 point)
uniform highp vec4 uL0;             // x light count, y strength, z lights with shadows, w debug
uniform highp vec4 uL1;             // x glare, y wet streaks, z fog glow, w lightning flash
uniform highp vec4 uMoon;           // xyz direction toward the moon, w moon light (0 = off)
uniform highp vec4 uMoon2;          // x glitter, y lamp glow, z neon glow, w AO multi-bounce
uniform highp vec4 uNL;             // x contrast, y exposure (EV), z colour shift, w night look amount (0 = off)
uniform highp vec4 uMud;            // 1.6.2 [MudPuddles] x mud water, y size variety, z rain splashes, w wall drips (0 = off)
uniform highp vec4 uSky2;           // x city glow, y stars, z Milky Way, w shooting stars (all * night)
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

// 1.6.2 [MudPuddles] rain drop impacts: a short bright crown per cell, random position and timing (rain hitting roads, sidewalks, roofs)
float Splash(vec2 p, float t){
  vec2 q = p * 5.0; vec2 i = floor(q), f = fract(q) - 0.5;
  float h = Hash12(i + 19.7);
  vec2 o = (vec2(Hash12(i + 2.3), Hash12(i + 8.9)) - 0.5) * 0.6;
  float ph = fract(t * (1.6 + h * 1.3) + h * 7.0);
  float r = length(f - o);
  return smoothstep(0.05, 0.0, abs(r - ph * 0.16)) * (1.0 - ph) * (1.0 - ph) * step(0.45, h);
}
// water running down walls / dripping from roof edges: thin animated streaks on vertical surfaces
float Drips(vec3 W, vec3 N, float t){
  vec2 hz = normalize(vec2(-N.y, N.x) + 1e-4);
  float u = dot(W.xy, hz) * 3.0; float col = floor(u);
  float h = Hash12(vec2(col, 4.1));
  float x = abs(fract(u) - 0.5 - (h - 0.5) * 0.6);
  float y = fract(W.z * 0.35 + t * (0.25 + h * 0.35) + h * 5.0);
  return smoothstep(0.06, 0.0, x) * smoothstep(0.0, 0.25, y) * smoothstep(1.0, 0.7, y) * step(0.35, h);
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


// ---------------------------------------------------------------- 1.6.0 night lighting
vec3 Albedo(vec3 c){ return mix(vec3(0.35), c / (Luma(c) + 0.05) * 0.35, 0.6); }   // albedo proxy of a forward-lit pixel (ZyZ dynlights)
float LightShadow(vec3 P0, vec3 L, float dl, float dist){
  float j = IGN(gl_FragCoord.xy + 17.0), len = min(dl - 0.3, 6.0);
  if (len <= 0.1) return 1.0;
  for (int i = 0; i < 6; ++i) {
    float t = (float(i) + j) / 6.0 * len;
    vec3 s = ToScreen(P0 + L * t);
    if (s.x < 0.0 || s.y < 0.0 || s.x > 1.0 || s.y > 1.0 || s.z >= 1.0) break;
    float dz = LinDepth(s.z) - LinDepth(texture(uDepth, s.xy).r);
    if (dz > 0.04 + dist * 0.001 && dz < 1.5) return 0.15 + 0.85 * float(i) / 6.0;
  }
  return 1.0;
}
// single-scattering of a point light along the view ray 0..tmax (closed form of the 1/r^2 integral)
float InScatter(vec3 Lp, vec3 V, float tmax){
  float t0 = dot(Lp, V); float h = max(length(Lp - V * t0), 0.15);
  return (atan((tmax - t0) / h) - atan(-t0 / h)) / h;
}
vec3 MultiBounce(float v, vec3 alb){
  vec3 a = 2.0404 * alb - 0.3324, b = -4.7951 * alb + 0.6417, c = 2.7552 * alb + 0.6903;
  return max(vec3(v), ((v * a + b) * v + c) * v);
}
vec3 NightSky(vec3 v, float dark){
  vec3 c = vec3(0.0);
  float vz = max(v.z, 0.0);
  c += vec3(1.0, 0.52, 0.22) * uSky2.x * exp(-vz * 7.0) * 0.10;                  // city light on the haze near the horizon
  if (vz > 0.02 && dark > 0.0) {
    vec2 q = vec2(atan(v.y, v.x) * 95.0, asin(clamp(v.z, -1.0, 1.0)) * 95.0);
    vec2 i = floor(q), f = fract(q) - 0.5;
    float h = Hash12(i);
    float tw = 0.65 + 0.35 * sin(uCam.w * (2.0 + h * 5.0) + h * 40.0);
    float st = step(0.985, h) * smoothstep(0.32, 0.0, length(f - (vec2(Hash12(i + 2.3), Hash12(i + 6.1)) - 0.5) * 0.4)) * tw;
    c += vec3(0.85, 0.9, 1.0) * st * uSky2.y * smoothstep(0.02, 0.2, vz) * 0.9;
    vec3 nb = normalize(vec3(0.35, 0.55, 0.76));                                  // Milky Way band
    float band = exp(-pow(dot(v, nb), 2.0) * 28.0) * (0.4 + Fbm(q * 0.04) * 1.2);
    c += vec3(0.55, 0.6, 0.85) * band * uSky2.z * 0.045 * smoothstep(0.02, 0.3, vz);
    float seed = floor(uCam.w / 5.0), ph = fract(uCam.w / 5.0);                   // one shooting star every ~5 s (some slots empty)
    if (Hash12(vec2(seed, 1.7)) > 0.4 && ph < 0.25) {
      float a = Hash12(vec2(seed, 3.1)) * 6.2831853, e = 0.35 + Hash12(vec2(seed, 5.3)) * 0.4;
      vec3 d0 = normalize(vec3(cos(a) * cos(e), sin(a) * cos(e), sin(e)));
      vec3 side = normalize(cross(d0, vec3(0.0, 0.0, 1.0)));
      vec3 d1 = normalize(d0 + side * 0.25 - vec3(0.0, 0.0, 0.08));
      float k = ph / 0.25;
      vec3 head = normalize(mix(d0, d1, k)), tail = normalize(mix(d0, d1, max(k - 0.25, 0.0)));
      vec3 seg = head - tail; float sl = max(dot(seg, seg), 1e-6);
      float u = clamp(dot(v - tail, seg) / sl, 0.0, 1.0);
      float dd = length(v - (tail + seg * u));
      c += vec3(1.0, 0.95, 0.85) * smoothstep(0.0025, 0.0, dd) * u * (1.0 - k) * uSky2.w * 2.0;
    }
  }
  return c * dark;
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
    vec3 alb = Albedo(col);
    col *= uMoon2.w > 0.5 ? MultiBounce(vis, clamp(col * 1.6, 0.0, 1.0)) : vec3(vis);
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
    // 1.6.2 [MudPuddles]: puddles grow with the wetness and shrink as the roads dry (CWeather::WetRoads), every patch has its own size,
    //   some are clear water, some muddy brown with a dark soaked rim. Roads, sidewalks and roofs all face up, so all of them get puddles.
    float puddle = 0.0, mudK = 0.0;
    if (wet > 0.01) {
      float pv = (Fbm(W.xy * 0.035 + 17.3) - 0.5) * uMud.y * 0.5;            // big-scale variety: wide puddle fields next to dry patches
      float pth = mix(0.66, 0.44, clamp(uW.x, 0.0, 1.0)) - pv;
      float pn = Fbm(W.xy * 0.12) + (Fbm(W.xy * 0.6 + 3.7) - 0.5) * 0.12;    // ragged edges
      puddle = smoothstep(pth, pth + 0.1, pn) * wet * uW.y;
      mudK = uMud.x * smoothstep(0.3, 0.7, Fbm(W.xy * 0.05 + 5.1));     // where the water is muddy
      float rim = smoothstep(pth - 0.08, pth, pn) * (1.0 - smoothstep(pth, pth + 0.1, pn)) * wet;
      vec3 mudC = vec3(0.32, 0.24, 0.15) * (0.35 + Luma(col) * 0.9);
      col = mix(col, col * vec3(0.72, 0.62, 0.5), rim * (0.35 + mudK * 0.4));  // soaked dark rim around every puddle
      col = mix(col, mudC, puddle * mudK * 0.55);                              // murky brown water
    }
    vec3 Nr = N;
    if (wet > 0.01 && uW.z > 0.0 && dist < 60.0) {
      vec2 rp = Ripples(W.xy, uCam.w) * uW.z * min(uOn.w * 1.5 + 0.2, 1.0);
      Nr = normalize(N + vec3(rp, 0.0) * (0.3 + puddle));
    }
    col *= 1.0 - wet * 0.25 - puddle * 0.2;
    if (uMud.z > 0.0 && wet > 0.01 && uOn.w > 0.01 && dist < 40.0)          // drops hitting the surface
      col += (Ambient(vec3(0.0, 0.0, 1.0)) * 0.6 + 0.08) * Splash(W.xy, uCam.w) * uMud.z * uOn.w * up * smoothstep(40.0, 15.0, dist) * 0.7;
    if (uMud.w > 0.0 && uW.x > 0.01 && dist < 50.0 && abs(N.z) < 0.35) {     // water running down walls
      float dr = Drips(W, N, uCam.w) * uMud.w * uW.x * smoothstep(50.0, 20.0, dist);
      col = col * (1.0 - dr * 0.25) + Ambient(N) * dr * 0.12;
    }
    // --- screen-space reflections
    float cosV = clamp(dot(-V, Nr), 0.0, 1.0);
    float fres = 0.04 + 0.96 * pow(1.0 - cosV, 5.0);
    float refl = max(uA.w * smoothstep(0.93, 0.99, N.z) * 0.35, wet * 0.35 + puddle * 0.65 * (1.0 - mudK * 0.45)) * uA.z;   // mud: duller mirror
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
    // --- [DynamicLights] every lamp / neon / car light the game reports (ZyZ window: (1 - (d/R)^4)^2 / d^2)
    if (uL0.x > 0.5) {
      vec3 acc = vec3(0.0), spec = vec3(0.0);
      int n = int(uL0.x), ns = int(uL0.z);
      vec3 P0 = C + N * (0.05 + dist * 0.002);
      for (int i = 0; i < 32; ++i) {
        if (i >= n) break;
        vec3 Lp = uLP[i].xyz - C; float R = uLP[i].w;
        float d2 = dot(Lp, Lp);
        if (d2 >= R * R) continue;
        float dl = sqrt(d2); vec3 L = Lp / max(dl, 1e-3);
        float x = d2 / (R * R); float win = 1.0 - x * x; win *= win;
        float att = win / max(d2 * 0.25, 0.35) * 1.2;
        float ndl = clamp((dot(N, L) + 0.05) / 1.05, 0.0, 1.0);
        if (ndl <= 0.0) continue;
        float sh = (i < ns && dist < 60.0) ? LightShadow(P0, L, dl, dist) : 1.0;
        vec3 li = uLC[i].rgb * att * ndl * sh;
        acc += li;
        vec3 H = normalize(L - V);
        spec += li * pow(max(dot(Nr, H), 0.0), 48.0) * (0.15 + wet * 2.5 + puddle * 3.0);
      }
      col += (alb * acc + spec) * uL0.y;
      if (uL0.w > 0.5) col = mix(col, vec3(1.0, 0.1, 0.8) * min(Luma(acc), 1.0), 0.35);
    }
    // --- [WetLightStreaks] long vertical reflections of the lights on wet / rainy roads
    if (uL1.y > 0.0 && uL0.x > 0.5 && wet > 0.02) {
      vec3 st = vec3(0.0);
      for (int i = 0; i < 12; ++i) {
        if (i >= int(uL0.x)) break;
        vec3 Lp = uLP[i].xyz; if (Lp.z <= C.z + 0.3) continue;
        vec3 Lm = vec3(Lp.xy, 2.0 * C.z - Lp.z);                                   // light mirrored in this pixel's ground plane
        vec3 s = ToScreen(Lm); if (s.z >= 1.0) continue;
        float dl = length(Lp - C);
        float dx = (vUV.x - s.x) * uRes.x * uRes.w, dy = vUV.y - s.y;
        float wd = 0.004 + 0.06 / (1.0 + dl * 0.15);
        float g = exp(-dx * dx / (wd * wd)) * exp(-dy * dy * 6.0) / (1.0 + dl * dl * 0.004);
        st += uLC[i].rgb * g;
      }
      col += st * wet * (0.4 + puddle) * uL1.y * 0.5;
    }
    // --- [Moon] light at night (directional, from the sun's opposite side of the sky)
    if (uMoon.w > 0.0 && uMoon.z > 0.02) col += alb * vec3(0.55, 0.65, 0.95) * max(dot(N, uMoon.xyz), 0.0) * uMoon.w * 0.12;
  } else {
    // --- sky
    if (uS.x > 0.0) { vec3 ps = PbrSky(V); col = mix(col, ps, uS.x * smoothstep(-0.02, 0.06, V.z)); }
    if (uS.z > 0.0 && uS.y > 0.0) { vec4 cl = Clouds(V); col = col * cl.a + cl.rgb; }
    if (uS.w > 0.0 && uS2.y > 0.05) col += Aurora(V) * uS.w * uS2.y;
    float dark = 1.0 - smoothstep(0.06, 0.35, Luma(sc.rgb));                    // only on a dark sky (never over the moon / bright clouds)
    if (uSky2.x + uSky2.y + uSky2.z + uSky2.w > 0.0) col += NightSky(V, dark);
  }
  float tmax = d < 0.99999 ? length(C) : 3000.0;
  // --- [Moon] glitter on the sea (the sea writes no depth: the plane z = 0 in front of the floor / sky)
  if (uMoon2.x > 0.0 && uMoon.z > 0.02 && V.z < -0.01 && uCam.z > 0.0) {
    float tw = -uCam.z / V.z;
    if (tw < tmax && tw < 1500.0) {
      vec2 wp = uCam.xy + V.xy * tw;
      vec2 g = vec2(Noise2(wp * 0.35 + uCam.w * 0.4), Noise2(wp * 0.35 - uCam.w * 0.35 + 9.0)) - 0.5;
      vec3 Nw = normalize(vec3(g * 0.35, 1.0));
      float sp = pow(max(dot(reflect(V, Nw), uMoon.xyz), 0.0), 220.0);
      float sparkle = step(0.72, Noise2(wp * 4.0 + uCam.w * 2.0));
      col += vec3(0.8, 0.85, 1.0) * sp * (0.4 + sparkle * 2.0) * uMoon2.x * 1.5;
    }
  }
  // --- [DynamicLights] fog glow + lamp halos + [LensGlare] star streaks
  if (uL0.x > 0.5 && (uL1.z > 0.0 || uL1.x > 0.0 || uMoon2.y > 0.0)) {
    vec3 gl = vec3(0.0);
    float aspect = uRes.x * uRes.w;
    for (int i = 0; i < 16; ++i) {
      if (i >= int(uL0.x)) break;
      vec3 Lp = uLP[i].xyz;
      vec3 Lc = uLC[i].rgb;
      if (uL1.z > 0.0) gl += Lc * InScatter(Lp, V, tmax) * uL1.z * 0.015;
      vec3 s = ToScreen(Lp);
      if (s.z >= 1.0 || s.x < -0.1 || s.y < -0.1 || s.x > 1.1 || s.y > 1.1) continue;
      float occ = (s.x > 0.0 && s.y > 0.0 && s.x < 1.0 && s.y < 1.0) ? step(LinDepth(s.z) - 0.6, LinDepth(texture(uDepth, s.xy).r)) : 0.0;
      if (occ <= 0.0) continue;
      vec2 dv = (vUV - s.xy) * vec2(aspect, 1.0);
      float dl = length(Lp), sz = clamp(2.5 / max(dl, 1.0), 0.004, 0.06);
      float r2 = dot(dv, dv);
      float halo = exp(-r2 / (sz * sz * 4.0));
      float glow = uLC[i].w > 1.5 ? uMoon2.z : uMoon2.y;
      gl += Lc * halo * glow * 0.25;
      if (uL1.x > 0.0) {
        float sx = exp(-abs(dv.y) / (sz * 0.06)) * exp(-abs(dv.x) / (sz * 6.0));
        float sy = exp(-abs(dv.x) / (sz * 0.06)) * exp(-abs(dv.y) / (sz * 4.0));
        gl += Lc * (sx + sy) * uL1.x * 0.18 / (1.0 + dl * 0.03);
      }
    }
    col += gl * uL0.y;
  }
  // --- lightning flash (CWeather::LightningFlash)
  if (uL1.w > 0.0) col += (d < 0.99999 ? col * 1.4 + vec3(0.04, 0.045, 0.06) : vec3(0.35, 0.38, 0.5)) * uL1.w;
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
  // --- [NightLook] exposure, contrast, cool shadows, neon saturation
  if (uNL.w > 0.0) {
    vec3 nc = col * exp2(uNL.y);
    nc = max((nc - 0.18) * uNL.x + 0.18, vec3(0.0));
    float l = Luma(nc);
    nc = mix(nc, l * vec3(0.55, 0.72, 1.15), uNL.z * (1.0 - smoothstep(0.0, 0.35, l)) * 0.6);
    float mx = max(nc.r, max(nc.g, nc.b)), sat = mx - min(nc.r, min(nc.g, nc.b));
    nc += nc * smoothstep(0.35, 0.9, mx) * sat * uMoon2.z * 0.6;
    col = mix(col, nc, uNL.w);
  }
  oColor = vec4(col, sc.a);
}
