// 1.6.2 host test of the 32-bit render-queue layout check (src32/ps_rq.cpp): accepts the derived 2.00 layout, rejects broken ones.
// build: g++ -std=c++17 -I../src32 -I../common -I../third_party host_rq32_test.cpp ../src32/ps_rq.cpp
#include "ps_rq.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static int fails = 0;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); ++fails; } } while (0)
static void W(char* q, int o, uint32_t v) { memcpy(q + o, &v, 4); }
static void Fill(char* q, uint32_t marker, int slot)
{
    memset(q, 0, 0x300);
    for (int i = 0; i < 125; ++i) W(q, i * 4, 0x10000000u + i * 0x40);
    W(q, slot * 4, marker);
    const int b = 0x1F4; q[b] = 1; q[b + 1] = 1; W(q, b + 4, 0x7000);  // threaded, mutex
    const uint32_t start = 0x20000000u;
    W(q, b + 12, start + 0x100000); W(q, b + 16, start); W(q, b + 20, start + 0x80); W(q, b + 24, start + 0x400); W(q, b + 28, start + 0x400); W(q, b + 32, 7);
}
int main()
{
    alignas(8) char q[0x300]; void* mk = (void*)(uintptr_t)0x12345678u;
    Fill(q, 0x12345678u, 47); CHECK(RQ32_DetectLayoutForTest(q, mk) == 0x1F4, "derived GTA SA 2.00 layout recognised");
    Fill(q, 0x9999u, 47);     CHECK(RQ32_DetectLayoutForTest(q, mk) == -1, "rqDebugMarker missing from the table -> off");
    Fill(q, 0x12345678u, 47); q[0x1F4] = 5; CHECK(RQ32_DetectLayoutForTest(q, mk) == -1, "bad flag byte -> off");
    Fill(q, 0x12345678u, 47); W(q, 0x1F4 + 28, 0x30000000u); CHECK(RQ32_DetectLayoutForTest(q, mk) == -1, "write pointer past the buffer end -> off");
    Fill(q, 0x12345678u, 47); W(q, 0x1F4 + 4, 0); CHECK(RQ32_DetectLayoutForTest(q, mk) == -1, "mutex flag set but no mutex -> off");
    Fill(q, 0x12345678u, 47); W(q, 0x1F4 + 32, 400); CHECK(RQ32_DetectLayoutForTest(q, mk) == -1, "last command id out of range -> off");
    printf(fails ? "RESULT: %d rq32 test(s) failed\n" : "RESULT: all rq32 tests passed\n", fails);
    return fails != 0;
}
