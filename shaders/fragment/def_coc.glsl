#version 300 es
// TyFMCGFX 1.5.0 [DepthOfField] Mode = Adaptive: circle of confusion per pixel (quarter resolution) from the scene depth and the
// auto-focus distance. r = blur amount 0..1, read by fragment/dof.glsl (uCoc).
#pragma include "def_common.glsl"
uniform mediump sampler2D uFocus;   // unit 1: 1x1 focus
uniform highp vec4 uP;              // x focus range (m, sharp zone), y transition scale, z near blur, w far blur
out vec4 oColor;
float Dec(vec4 c){ return exp2((c.r * 255.0 * 256.0 + c.g * 255.0) / 65535.0 * 11.0) - 1.0; }
void main(){
  float f = Dec(texture(uFocus, vec2(0.5)));
  float d = texture(uDepth, vUV).r;
  float z = d >= 0.99999 ? 3000.0 : LinDepth(d);
  float range = uP.x + f * 0.25;
  float c = (z - f) / (range * uP.y + 1.0);
  float coc = c < 0.0 ? clamp(-c * uP.z, 0.0, 1.0) : clamp((c - 1.0) * uP.w, 0.0, 1.0);
  oColor = vec4(coc, 0.0, 0.0, 1.0);
}
