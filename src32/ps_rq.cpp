#include "ps_rq.h"
#include "ps_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

const int      kTable = 125;                             // command table entries (0x3E8 / 8 on arm64 -> 0x1F4 / 4 here)
const uint64_t kMagic = 0x3258464750474650ull;           // "PFGPGFX2": OUR commands in the rqDebugMarker slot (32-bit build)
const uint32_t kCmdSize = 4 + 8 + 8;                     // id + magic + payload
// field offsets from the end of the command table (arm64 offset - 0x3E8, pointer fields halved)
enum { F_THR = 0, F_MTX = 1, F_MUTEX = 4, F_END = 12, F_P16 = 16, F_P20 = 20, F_COM = 24, F_WP = 28, F_LAST = 32 };

typedef void (*CmdFn)(char**);
void**  g_rqVar = nullptr;
void   (*g_mutexObtain)(void*) = nullptr;
void   (*g_mutexRelease)(void*) = nullptr;
void   (*g_process)(void*) = nullptr;
void*   g_dbgMarker = nullptr;
CmdFn   g_prev = nullptr;
bool    g_regTried = false;
int     g_base = -1;
PSRqHandler g_handler = nullptr;
PSRqStats S;

void Note(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Note(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt); vsnprintf(S.note, sizeof(S.note), fmt, ap); va_end(ap);
    PS_Log("rq32: %s", S.note);
}

inline uint32_t U32(const char* p) { uint32_t v; memcpy(&v, p, 4); return v; }
inline void     W32(char* p, uint32_t v) { memcpy(p, &v, 4); }

void OurCommand(char** pp)
{
    uint64_t m = 0; memcpy(&m, *pp, 8);
    if (m != kMagic) { if (g_prev) g_prev(pp); return; }
    *pp += 8;
    uint64_t pay = 0; memcpy(&pay, *pp, 8); *pp += 8;
    ++S.ran;
    if (g_handler) g_handler((int)(pay & 0xff));
}

// every check must pass: flags 0/1, mutex present when used, end >= write >= committed, the two other buffer pointers in the
// same buffer, buffer below 64 MB, last command id a valid slot
bool FieldsOk(const char* rq, int b)
{
    const uint8_t thr = (uint8_t)rq[b + F_THR], mtx = (uint8_t)rq[b + F_MTX];
    if (thr > 1 || mtx > 1) return false;
    if (mtx && !U32(rq + b + F_MUTEX)) return false;
    const uint32_t end = U32(rq + b + F_END), com = U32(rq + b + F_COM), wp = U32(rq + b + F_WP);
    const uint32_t p16 = U32(rq + b + F_P16), p20 = U32(rq + b + F_P20), last = U32(rq + b + F_LAST);
    if (!end || !com || !wp || !p16 || !p20) return false;
    if (!(com <= wp && wp <= end)) return false;
    const uint32_t lo = p16 < p20 ? p16 : p20;
    if (lo > com || end - lo > (64u << 20) || p16 > end || p20 > end) return false;
    if (last >= (uint32_t)kTable) return false;
    return true;
}

int DetectLayout(char* rq, void* marker, int* slotOut)
{
    int slot = -1;
    const uint32_t mk = (uint32_t)(uintptr_t)marker;
    for (int i = 0; i < kTable; ++i) if (U32(rq + i * 4) == mk) { slot = i; break; }
    if (slot < 0) return -1;
    if (slotOut) *slotOut = slot;
    const int b = kTable * 4;
    return FieldsOk(rq, b) ? b : -1;
}

bool Register()
{
    if (S.ready) return true;
    if (g_regTried || !S.symbols || !g_rqVar) return false;
    char* rq = (char*)*g_rqVar;
    if (!rq) return false;                                  // created a bit later by the game: try again next frame
    g_regTried = true;
    int slot = -1;
    const int b = DetectLayout(rq, g_dbgMarker, &slot);
    if (slot < 0) { S.badLayout = true; Note("rqDebugMarker not in the command table (another mod took it, or not GTA SA 2.00) - swap-time effects"); return false; }
    if (b < 0) { S.badLayout = true; Note("render queue layout not recognised - swap-time effects (HUD may be graded)"); return false; }
    g_base = b;
    void** table = (void**)rq;
    g_prev = (CmdFn)table[slot];
    __atomic_store_n(&table[slot], (void*)&OurCommand, __ATOMIC_SEQ_CST);
    S.slot = slot; S.base = b; S.ready = true; S.threaded = rq[b + F_THR] != 0;
    PS_Log("rq32: in-frame post effects on (command slot %d, fields at +0x%x, queue %s)", slot, b, S.threaded ? "threaded" : "not threaded");
    return true;
}

} // namespace

bool RQ32_Init(PSRqLookup h)
{
    if (!h) return false;
    g_rqVar        = (void**)h("renderQueue");
    g_mutexObtain  = (void (*)(void*))h("_Z14OS_MutexObtainPv");
    g_mutexRelease = (void (*)(void*))h("_Z15OS_MutexReleasePv");
    g_dbgMarker    = h("_Z24RQ_Command_rqDebugMarkerRPc");
    g_process      = (void (*)(void*))h("_ZN11RenderQueue7ProcessEv");
    S.symbols = g_rqVar && g_mutexObtain && g_mutexRelease && g_dbgMarker && g_process;
    if (!S.symbols) Note("render-queue symbols missing (renderQueue %d, mutex %d/%d, rqDebugMarker %d, Process %d) - swap-time effects",
                         g_rqVar != nullptr, g_mutexObtain != nullptr, g_mutexRelease != nullptr, g_dbgMarker != nullptr, g_process != nullptr);
    return S.symbols;
}

void RQ32_SetHandler(PSRqHandler fn) { g_handler = fn; }
bool RQ32_Ready() { return S.ready; }
const PSRqStats& RQ32_Stats() { return S; }

bool RQ32_Post(int ev)
{
    if (!Register()) return false;
    char* rq = (char*)*g_rqVar;
    if (!rq) return false;
    const int b = g_base;
    if (!FieldsOk(rq, b)) { ++S.skippedFull; return false; }           // re-checked every time: never write on a state we do not understand
    const uint32_t wp = U32(rq + b + F_WP), end = U32(rq + b + F_END);
    if (end - wp < kCmdSize + 64) { ++S.skippedFull; return false; }
    const int32_t id = S.slot; const uint64_t pay = (uint64_t)(ev & 0xff);
    W32(rq + b + F_LAST, (uint32_t)id);
    char* w = (char*)(uintptr_t)wp;
    memcpy(w, &id, 4); memcpy(w + 4, &kMagic, 8); memcpy(w + 12, &pay, 8);
    const uint32_t np = wp + kCmdSize;
    W32(rq + b + F_WP, np);
    const bool mtx = rq[b + F_MTX] != 0;
    void* m = mtx ? (void*)(uintptr_t)U32(rq + b + F_MUTEX) : nullptr;
    if (m) g_mutexObtain(m);
    uint32_t* com = (uint32_t*)(rq + b + F_COM);
    __atomic_fetch_add(com, np - __atomic_load_n(com, __ATOMIC_SEQ_CST), __ATOMIC_SEQ_CST);   // committed end = write pointer
    if (m) g_mutexRelease(m);
    ++S.posted;
    if (!rq[b + F_THR]) g_process(rq);
    return true;
}

int RQ32_DetectLayoutForTest(char* rq, void* marker) { return DetectLayout(rq, marker, nullptr); }
