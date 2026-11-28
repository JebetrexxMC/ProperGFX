#pragma once
// 1.6.1: replay guard for the shadow / camera-depth replays (both builds).
//
// The 1.6.0 crash (SIGSEGV in libGLES_mali memcpy under glDrawElements, called from the shadow cascade replay) had two causes:
//   1) the replay only set attributes 0 / 1. The game's other vertex attributes (normal, colour, bone weights ...) stayed ENABLED with the
//      pointers of the game's last draw, so the driver fetched them for our draw too - with our indices, past the end of that other buffer.
//   2) nothing checked that the indices of a remembered draw still fit into its vertex buffer. The game frees and re-creates buffers; a new
//      buffer with the same name and the same byte size passed every 1.6.0 check, its indices pointed anywhere.
// Fix: every attribute except the ones a replay sets is disabled for the replay (PSGLState restores them), and the largest index of every
// draw is known (from a CPU copy of the 16-bit index uploads) and checked against the vertex buffer size before the draw is issued.
#include <GLES2/gl2.h>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <stdint.h>
#include <string.h>

namespace psrg {

struct Store
{
    std::mutex mx;
    std::unordered_map<GLuint, std::vector<uint16_t>> idx;   // index buffer name -> CPU copy (ushort)
    std::unordered_map<GLuint, bool> unknown;                // mapped / too big: contents unknown
    size_t bytes = 0;
};
inline Store& S() { static Store s; return s; }
static const size_t kMaxBuffer = 1u << 20;                   // 1 MB per index buffer
static const size_t kMaxTotal  = 48u << 20;                  // 48 MB for all copies

// glBufferData (sub = false) / glBufferSubData (sub = true) on GL_ELEMENT_ARRAY_BUFFER `id`; any thread
inline void OnIndexData(GLuint id, intptr_t off, intptr_t n, const void* data, bool sub)
{
    if (!id) return;
    Store& s = S(); std::lock_guard<std::mutex> l(s.mx);
    auto& v = s.idx[id];
    if (!sub)
    {
        s.bytes -= v.size() * 2; v.clear();
        if (!data || n <= 0) { s.unknown.erase(id); if (!data && n > 0) { v.assign((size_t)n / 2, 0); s.bytes += v.size() * 2; } return; }   // NULL = reserve (zeros)
        if ((size_t)n > kMaxBuffer || s.bytes + (size_t)n > kMaxTotal) { s.unknown[id] = true; return; }
        v.resize((size_t)n / 2); memcpy(v.data(), data, v.size() * 2); s.bytes += v.size() * 2; s.unknown.erase(id);
        return;
    }
    if (!data || n <= 0 || off < 0) return;
    const size_t end = ((size_t)off + (size_t)n) / 2;
    if (end > v.size() || (off & 1)) { s.unknown[id] = true; return; }
    memcpy((char*)v.data() + off, data, (size_t)n & ~(size_t)1);
}
inline void OnIndexMapped(GLuint id) { if (!id) return; Store& s = S(); std::lock_guard<std::mutex> l(s.mx); s.unknown[id] = true; }
inline void OnDelete(GLsizei n, const GLuint* ids)
{
    if (!ids || n <= 0) return;
    Store& s = S(); std::lock_guard<std::mutex> l(s.mx);
    for (GLsizei i = 0; i < n; ++i)
    {
        auto it = s.idx.find(ids[i]);
        if (it != s.idx.end()) { s.bytes -= it->second.size() * 2; s.idx.erase(it); }
        s.unknown.erase(ids[i]);
    }
}
inline void Clear() { Store& s = S(); std::lock_guard<std::mutex> l(s.mx); s.idx.clear(); s.unknown.clear(); s.bytes = 0; }

// largest index used by glDrawElements(count, GL_UNSIGNED_SHORT, off) on `ibo`; -1 = unknown
inline int MaxIndex(GLuint ibo, uintptr_t off, int count)
{
    Store& s = S(); std::lock_guard<std::mutex> l(s.mx);
    if (s.unknown.count(ibo)) return -1;
    auto it = s.idx.find(ibo);
    if (it == s.idx.end() || (off & 1) || count <= 0) return -1;
    const size_t b = off / 2, e = b + (size_t)count;
    if (e > it->second.size()) return -2;                    // the draw does not even fit the index buffer
    uint16_t m = 0; const uint16_t* p = it->second.data();
    for (size_t i = b; i < e; ++i) if (p[i] > m) m = p[i];
    return (int)m;
}

inline int TypeBytes(GLint type)
{
    switch (type) { case GL_FLOAT: case GL_FIXED: return 4; case GL_SHORT: case GL_UNSIGNED_SHORT: return 2; case GL_BYTE: case GL_UNSIGNED_BYTE: return 1; default: return 4; }
}
// does vertex `maxIdx` of the attribute (ptr / stride / size / type) lie inside a buffer of `vboBytes`?
inline bool AttrFits(uintptr_t ptr, GLint stride, GLint size, GLint type, int maxIdx, GLint vboBytes)
{
    if (vboBytes <= 0 || size <= 0) return false;
    const long long elem = (long long)size * TypeBytes(type);
    const long long st = stride > 0 ? stride : elem;
    const long long last = (long long)ptr + (long long)(maxIdx < 0 ? 0 : maxIdx) * st + elem;
    return last <= (long long)vboBytes;
}

// disable every vertex attribute from `first` up (the caller's PSGLState::Restore re-enables what the game had)
inline void IsolateAttribs(int first, int nattr)
{
    static GLint maxA = 0;
    if (!maxA) { glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxA); if (maxA <= 0) maxA = 8; }
    const int n = nattr < maxA ? nattr : maxA;
    for (int i = first; i < n; ++i) glDisableVertexAttribArray((GLuint)i);
}

} // namespace psrg
