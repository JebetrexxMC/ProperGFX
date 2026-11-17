#version 300 es
// Proper GFX 1.5.0 - depth-aware (bilateral) blur of the AO / GI buffer and the volumetric buffer. Separable: run horizontal + vertical.
#pragma include "def_common.glsl"
uniform mediump sampler2D uTex;     // unit 1
uniform highp vec2 uDir;            // one texel of uTex along the blur axis
out vec4 oColor;
void main(){
  float zc = LinDepth(texture(uDepth, vUV).r);
  vec4 acc = texture(uTex, vUV) * 0.38; float ws = 0.38;
  for (int i = 1; i <= 3; ++i) {
    float w = i == 1 ? 0.22 : (i == 2 ? 0.1 : 0.04);
    for (int s = -1; s <= 1; s += 2) {
      vec2 uv = vUV + uDir * float(i * s);
      float z = LinDepth(texture(uDepth, uv).r);
      float k = w * exp(-abs(z - zc) / max(zc * 0.04, 0.05));
      acc += texture(uTex, uv) * k; ws += k;
    }
  }
  oColor = acc / ws;
}
