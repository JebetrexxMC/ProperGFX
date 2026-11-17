#version 300 es
// Proper GFX 1.5.0 soft shadow mask: separable 9-tap gaussian of the shadow mask, depth aware (an edge of a building does not
// bleed its shadow onto the sky / the street far behind it). Run twice (horizontal, vertical) right after the mask pass.
precision highp float;
uniform mediump sampler2D uMask;     // unit 0: mask (or the horizontal result)
uniform highp sampler2D uDepth;      // unit 1: scene depth (full size)
uniform highp vec2 uDir;             // one mask texel along the blur axis (uv)
uniform highp vec4 uP;               // x mask -> depth pixel scale, y z = projection A, B (linear depth = B / (ndc + A))
in highp vec2 vUV;
out vec4 oColor;
float Lin(vec2 uv){ highp float d = texture(uDepth, uv).r * 2.0 - 1.0; return abs(uP.z / (d + uP.y)); }
void main(){
  const float w0 = 0.2270270, w1 = 0.1945946, w2 = 0.1216216, w3 = 0.0540540, w4 = 0.0162162;
  float zc = Lin(vUV);
  vec3 acc = texture(uMask, vUV).rgb * w0; float ws = w0;
  for (int i = 1; i <= 4; ++i) {
    float w = i == 1 ? w1 : (i == 2 ? w2 : (i == 3 ? w3 : w4));
    for (int s = -1; s <= 1; s += 2) {
      vec2 uv = vUV + uDir * float(i * s) * 1.25;
      float z = Lin(uv);
      float k = w * exp(-abs(z - zc) / max(zc * 0.03, 0.05));
      acc += texture(uMask, uv).rgb * k; ws += k;
    }
  }
  oColor = vec4(acc / ws, 1.0);
}
