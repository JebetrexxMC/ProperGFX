#version 300 es
// TyFMCGFX 1.6.1 [DepthOfField] Mode = Real depth: circle of confusion per pixel (quarter resolution) from the scene depth, thin-lens style:
// blur grows with |z - focus| / z (like a camera lens), with a sharp zone around the focus distance.
// r = blur of this pixel 0..1 (near or far), g = NEAR blur only (foreground that spills over sharp things behind it). Read by fragment/dof.glsl (uCoc).
#pragma include "def_common.glsl"
uniform mediump sampler2D uFocus;   // unit 1: 1x1 focus (auto focus)
uniform highp vec4 uP;              // x sharp zone (m), y (unused, 1), z near blur, w far blur
uniform highp vec4 uQ;              // x manual focus distance in metres (0 = auto focus), y aperture (blur scale)
out vec4 oColor;
float Dec(vec4 c){ return exp2((c.r * 255.0 * 256.0 + c.g * 255.0) / 65535.0 * 11.0) - 1.0; }
void main(){
  float f = uQ.x > 0.05 ? uQ.x : Dec(texture(uFocus, vec2(0.5)));
  f = max(f, 0.3);
  float d = texture(uDepth, vUV).r;
  float z = d >= 0.99999 ? 3000.0 : max(LinDepth(d), 0.05);
  float zone = uP.x + f * 0.15;                                   // depth of the sharp zone in metres
  float far  = max(z - f - zone, 0.0) / max(z, 0.1);             // 0 .. ~1 for the far field
  float near = max(f - zone * 0.5 - z, 0.0) / max(z, 0.1);       // 0 .. large for close objects
  float cf = clamp(far  * uP.w * uQ.y * 2.2, 0.0, 1.0);
  float cn = clamp(near * uP.z * uQ.y * 1.4, 0.0, 1.0);
  oColor = vec4(max(cf, cn), cn, 0.0, 1.0);
}
