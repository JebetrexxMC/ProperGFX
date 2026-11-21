// TyFMCGFX 1.6.0 - dynamic light pool (port of the ZyZGfx 2.0 "dynlights" / "beams" modules)
// Game thread: CShadows::StoreStaticShadow / StoreCarLightShadow, CPointLights::AddLight and
// CVehicle::DoHeadLightBeam feed the pool. Render thread: Lights_Snapshot picks the nearest ones.
#pragma once
#include <stdint.h>

struct PSLight { float pos[3]; float radius; float col[3]; float intensity; int kind; }; // kind 0 lamp, 1 car, 2 point
struct PSBeam  { float pos[3]; float dir[3]; float intensity; float pad; };

void Lights_Add(const float pos[3], float radius, float r, float g, float b, float intensity, int kind);
void Lights_AddBeam(const float pos[3], const float dir[3], float intensity);
// nearest `maxL` lights seen within the last ~180 ms; frozen = keep everything (game paused)
int  Lights_Snapshot(const float cam[3], bool frozen, PSLight* out, int maxL, PSBeam* beams, int maxB, int* nBeams);
int  Lights_PoolCount();
