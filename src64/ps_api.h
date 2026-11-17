#pragma once
#include "ps_settings.h"
#include "propergfx_api.h"
void        PSApi_Init(PSSettings* cfg, const char* dir, const char* iniPath);
IProperGFX* PSApi_Get();
void        PSApi_Tick();   // render thread, once per frame: applies the queued changes
