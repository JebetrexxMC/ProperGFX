// [DepthOfField]
//   Mode 2 = Real depth (1.6.1, default): a real depth of field from the scene depth. ps_deferred.cpp writes the circle of confusion
//            (def_focus = auto focus, def_coc = near / far blur per pixel). Here: a 16-tap golden-angle disc gather from the FULL-res scene,
//            every tap weighted by its OWN blur (scatter-as-gather) - a sharp object in front never smears into the blurred background,
//            the blurred foreground spills over the sharp background like a real lens. Large blurs read the pre-blurred 1/8 copy (no ringing).
//   Mode 0 = Tilt-shift  (screen band - a miniature look, not depth based)
//   Mode 1 = Center focus (screen circle - not depth based)
// Underwater (UnderWaterness > 0) adds an all-over blur ([DepthOfField] UnderwaterBlur).
// uDof = 1/8-res blurred scene. uDofA = (mode, focus position, focus width, strength)  uDofB = (bokeh boost, radius, underwater blur, 0)
uniform sampler2D uDof;
uniform vec4 uDofA;
uniform vec4 uDofB;
uniform sampler2D uCoc;      // Real depth: r = blur, g = near blur (black when the scene depth is not available)

float dofScreen(vec2 uv, vec2 res){
  float m;
  if (uDofA.x < 0.5) m = smoothstep(uDofA.z, uDofA.z + 0.28, abs(uv.y - uDofA.y));
  else {
    vec2 d = (uv - vec2(0.5, uDofA.y)) * vec2(res.x / res.y, 1.0);
    m = smoothstep(uDofA.z, uDofA.z + 0.45, length(d));
  }
  return m * uDofA.w;
}

// 7-tap disc gather from the blurred copy (screen modes / underwater)
vec3 dofBlur(vec2 uv, vec2 px, float m){
  vec2 r = px * 8.0 * uDofB.y * m;
  vec3 acc = vec3(0.0); float wsum = 0.0;
  for (int i = 0; i < 7; i++) {
    float a = float(i) * 0.8975979;
    vec2 o = (i == 0) ? vec2(0.0) : vec2(cos(a), sin(a)) * r;
    vec3 c = texture2D(uDof, uv + o).rgb;
    float l = lum3(c);
    float w = 1.0 + uDofB.x * l * l * 4.0;
    acc += c * w; wsum += w;
  }
  return acc / wsum;
}

vec3 dofDepth(vec3 col, vec2 uv, vec2 px, vec2 res){
  float rmax = 12.0 * uDofB.y * (res.y / 720.0) * clamp(uDofA.w * 1.25, 0.0, 1.5);   // largest blur radius in pixels
  vec2 c0 = texture2D(uCoc, uv).rg;
  // near-field dilation: a blurred foreground edge grows over what is behind it
  float nd = c0.g;
  for (int k = 0; k < 4; k++) {
    float a = float(k) * 1.5707963 + 0.785398;
    nd = max(nd, texture2D(uCoc, uv + vec2(cos(a), sin(a)) * px * rmax * 0.6).g * 0.85);
  }
  float cc = max(c0.r, nd);
  float rad = cc * rmax;
  if (rad < 0.6) return col;
  float big = clamp((rad - 9.0) / 10.0, 0.0, 1.0);           // large blur: the pre-blurred copy, no sparse-tap ringing
  float rot = ign(gl_FragCoord.xy) * 6.2831853;
  vec3 acc = col; float ws = 1.0;
  for (int i = 1; i <= 16; i++) {
    float fi = float(i);
    float r = sqrt(fi / 16.0) * rad;
    float a = fi * 2.3999632 + rot;                          // golden angle
    vec2 tuv = uv + vec2(cos(a), sin(a)) * r * px;
    vec3 c = big > 0.0 ? mix(texture2D(uScene, tuv).rgb, texture2D(uDof, tuv).rgb, big) : texture2D(uScene, tuv).rgb;
    vec2 tc = texture2D(uCoc, tuv).rg;
    float tr = max(tc.r, tc.g) * rmax;                       // how far this tap's own blur reaches
    float w = clamp(tr - r + 1.5, 0.0, 1.0);                 // a sharp tap does not reach us -> no halo around in-focus edges
    float l = lum3(c);
    w *= 1.0 + uDofB.x * l * l * 4.0;                        // bokeh: bright out-of-focus highlights weigh more
    acc += c * w; ws += w;
  }
  return mix(col, acc / ws, smoothstep(0.0, 0.12, cc));
}

vec3 applyDof(vec3 col, vec2 uv, vec2 px, vec2 res){
  float uw = clamp(UnderWaterness, 0.0, 1.0) * uDofB.z;
  if (uDofA.x > 1.5) {
    col = dofDepth(col, uv, px, res);
    if (uw > 0.003) col = mix(col, dofBlur(uv, px, uw), uw);
    return col;
  }
  float m = clamp(max(dofScreen(uv, res), uw), 0.0, 1.0);
  if (m < 0.003) return col;
  return mix(col, dofBlur(uv, px, m), m);
}
