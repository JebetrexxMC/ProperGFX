#version 300 es
// TyFMCGFX 1.5.0 [DepthOfField] Mode = Adaptive: auto focus. 1x1 target: distance at the screen centre (9 taps, the nearest wins a
// bit more), adapted over time like a camera's focus motor. Packed into RG8 (log-encoded, 16 bit) - GLES 3.0 cannot render to float.
#pragma include "def_common.glsl"
uniform mediump sampler2D uPrev;    // unit 1: previous focus (1x1)
uniform highp vec4 uP;              // x adapt 0..1, y reset
out vec4 oColor;
float Dec(vec4 c){ return exp2((c.r * 255.0 * 256.0 + c.g * 255.0) / 65535.0 * 11.0) - 1.0; }
vec4 Enc(float z){ float v = clamp(log2(z + 1.0) / 11.0, 0.0, 1.0) * 65535.0; float hi = floor(v / 256.0); return vec4(hi / 255.0, (v - hi * 256.0) / 255.0, 0.0, 1.0); }
void main(){
  float z = 0.0, ws = 0.0;
  for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
    float l = min(LinDepth(texture(uDepth, vec2(0.5) + vec2(float(x), float(y)) * 0.04).r), 1500.0);
    float w = 1.0 / (1.0 + l * 0.02);
    z += l * w; ws += w;
  }
  z /= ws;
  float prev = Dec(texture(uPrev, vec2(0.5)));
  oColor = Enc(uP.y > 0.5 ? z : mix(prev, z, uP.x));
}
