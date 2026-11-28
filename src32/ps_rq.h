// TyFMCGFX 1.6.2 (32-bit) - render-queue events for GTA SA 2.00 armeabi-v7a, so the post effects run INSIDE the frame
// (after the 3D world, before the HUD / radar / menus are drawn) instead of over the finished back buffer at swap time.
//
// Same technique as src64/ps_scene.cpp (libZyZyGFX64): the game thread records GL work into RenderQueue, the render thread
// replays it. A hook on CPostEffects::MobileRender (game thread) appends OUR command to the queue; it runs on the render thread
// exactly between the world and the 2D pass. The command id is the rqDebugMarker slot of the queue's command table.
//
// The 2.00 arm32 RenderQueue layout is not published, so it is DERIVED from the 2.10 arm64 one (every field pointer-sized:
// 125-entry command table, then the flags / mutex / buffer pointers) and VERIFIED at runtime before anything is written:
// the rqDebugMarker handler must be in the table, the flags must be 0/1, the buffer pointers must be ordered and inside one
// buffer. If anything does not match, events stay off and the 1.6.1 swap-time path is used (no crash, HUD as before).
#pragma once
#include <stdint.h>

enum PSRqEvent { PSRQ_AFTER_SCENE = 2, PSRQ_POST = 4 };
typedef void (*PSRqHandler)(int ev);
typedef void* (*PSRqLookup)(const char* name);

struct PSRqStats
{
    bool  symbols = false, ready = false, badLayout = false, threaded = false;
    int   slot = -1, base = -1;
    uint32_t posted = 0, ran = 0, skippedFull = 0;
    char  note[160] = "";
};

bool  RQ32_Init(PSRqLookup lookup);
void  RQ32_SetHandler(PSRqHandler fn);
bool  RQ32_Post(int ev);              // game thread
bool  RQ32_Ready();
const PSRqStats& RQ32_Stats();

// host test: the layout check on a fake queue (returns the detected field base or -1)
int   RQ32_DetectLayoutForTest(char* rq, void* dbgMarker);
