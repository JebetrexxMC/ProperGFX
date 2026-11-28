// host test for common/ps_sunshadow.h: g++ -std=c++17 -I../common host_sunshadow_test.cpp
#include <stdio.h>
#include <string>
#include "ps_sunshadow.h"
static float arr[6][16]; static float* ptr[6];
static void* Get(const char* n) {
    static const char* k[6] = { "FrontX9", "FrontY9", "SideX9", "SideY9", "DisplacementX9", "DisplacementY9" };
    for (int i = 0; i < 6; ++i) if (strstr(n, k[i]) && strstr(n, "ValueProxy")) return &ptr[i];
    return nullptr;
}
static int fails = 0;
static void Check(bool c, const char* t) { printf("%s  %s\n", c ? "PASS" : "FAIL", t); if (!c) ++fails; }
int main() {
    for (int i = 0; i < 6; ++i) ptr[i] = arr[i];
    Check(pssun::Init(Get), "proxies resolved");
    float sun[48] = {}; int idx = 3; sun[9] = 0.6f; sun[10] = 0.f; sun[11] = 0.8f;       // sun in +X, high
    arr[0][3] = 0.f; arr[1][3] = 1.2f; arr[2][3] = 0.9f; arr[3][3] = 0.f; arr[4][3] = 0.f; arr[5][3] = 0.3f;
    pssun::Apply(sun, &idx, 1.f, 1.f, false);
    Check(fabsf(arr[0][3] + 1.2f) < 1e-4f && fabsf(arr[1][3]) < 1e-4f, "front points away from the sun, game length kept");
    Check(fabsf(arr[2][3]) < 1e-4f && fabsf(arr[3][3] - 0.9f) < 1e-4f, "side perpendicular, game width kept");
    for (int i = 0; i < 20; ++i) pssun::Apply(sun, &idx, 1.5f, 1.f, false);       // the game did not rewrite: no compounding
    Check(fabsf(arr[0][3] + 1.8f) < 1e-4f, "no compounding when the game keeps our values");
    sun[11] = 0.2f; arr[0][3] = 0.f; arr[1][3] = 1.f; pssun::Apply(sun, &idx, 1.f, 1.f, true);
    Check(arr[0][3] < -2.0f, "low sun -> longer shadow");
    sun[11] = -0.3f; arr[0][3] = 5.f; pssun::Apply(sun, &idx, 1.f, 1.f, true);
    Check(arr[0][3] == 5.f, "night: game values untouched");
    idx = 40; arr[0][3] = 5.f; pssun::Apply(sun, &idx, 1.f, 1.f, true);
    Check(arr[0][3] == 5.f, "bad slot index ignored");
    printf("RESULT: %s\n", fails ? "FAILED" : "all sunshadow tests passed");
    return fails;
}
