// [SimulateHDR] tone mappers. TM_MODE is set by the mod: 1 = ACES, 2 = Timothy (Lottes), 3 = Reinhard (extended)
#if TM_MODE == 1
vec3 tonemap(vec3 x){ x *= 0.7232; return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0); }
#elif TM_MODE == 2
vec3 tonemap(vec3 x){ x = max(x*0.7203, vec3(0.0001)); return pow(x, vec3(1.6)) / (pow(x, vec3(1.5632))*1.07304 + 0.16742); }
#elif TM_MODE == 3
vec3 tonemap(vec3 x){ x *= 1.1998; return x*(1.0 + x/16.0)/(1.0 + x); }
#elif TM_MODE == 4
// AgX (Troy Sobotka's AgX, minimal fit). Ported from Google Filament's ToneMapper (Apache-2.0, see THIRD_PARTY_NOTICES.md)
vec3 agxCurve(vec3 x){ vec3 x2 = x*x; vec3 x4 = x2*x2; return 15.5*x4*x2 - 40.14*x4*x + 31.96*x4 - 6.868*x2*x + 0.4298*x2 + 0.1191*x - 0.00232; }
vec3 tonemap(vec3 c){
  const mat3 agxIn  = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992, 0.878468636469772, 0.0784336, 0.0792237451477643, 0.0791661274605434, 0.879142973793104);
  const mat3 agxOut = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368, 1.15190312990417, -0.0980434501171241, -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
  c = agxIn * (c * 0.62);
  c = clamp(log2(max(c, vec3(1e-10))), -12.47393, 4.026069);
  c = (c + 12.47393) / 16.5;
  c = agxOut * agxCurve(c);
  c = max(c, vec3(0.0));
  return c * c;                                    // AgX output is display-encoded: back to the composite's linear (gamma 2.0)
}
#elif TM_MODE == 5
// Khronos PBR Neutral. Ported from Google Filament's ToneMapper (Apache-2.0, see THIRD_PARTY_NOTICES.md)
vec3 tonemap(vec3 c){
  const float startC = 0.76; const float desat = 0.15;
  float x = min(c.r, min(c.g, c.b));
  float off = x < 0.08 ? x - 6.25*x*x : 0.04;
  c -= off;
  float peak = max(c.r, max(c.g, c.b));
  if (peak < startC) return c;
  const float d = 1.0 - startC;
  float np = 1.0 - d*d / (peak + d - startC);
  c *= np / peak;
  float g = 1.0 - 1.0 / (desat * (peak - np) + 1.0);
  return mix(c, vec3(np), g);
}
#endif
