#include "ps_deferred.h"
#include "ps_shadow.h"
#include "ps_glsl.h"
#include "ps_log.h"
#include <GLES3/gl3.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <string>

namespace {

struct RT { GLuint tex = 0, fbo = 0; int w = 0, h = 0; };
struct Prog { GLuint id = 0; bool tried = false; const char* fs = ""; };

struct DState
{
    bool checked = false, es3 = false;
    Prog ao { 0, false, "fragment/def_ao.glsl" }, bil { 0, false, "fragment/def_bilateral.glsl" }, vol { 0, false, "fragment/def_volume.glsl" },
         comp { 0, false, "fragment/def_composite.glsl" }, focus { 0, false, "fragment/def_focus.glsl" }, coc { 0, false, "fragment/def_coc.glsl" },
         taa { 0, false, "fragment/def_taa.glsl" }, fsr { 0, false, "fragment/def_fsr.glsl" };
    GLuint copyTex = 0; int cw = 0, ch = 0; GLenum copyFmt = GL_RGBA;
    RT aoA, aoB, volA, volB, compRT, focusRT[2], cocRT, hist[2], post;
    int focusIdx = 0, histIdx = 0; bool focusReset = true, histReset = true;
    double prevPV[16]; float prevCam[3] = {0, 0, 0}; bool havePrev = false;
    GLuint cocOut = 0;
    float frameNo = 0.f;
    PSDeferredFrame f;
    PSDeferredStats st;
};
DState D;

void Note(const char* t) { snprintf(D.st.note, sizeof(D.st.note), "%s", t); }

bool Compile(GLuint& out, GLenum type, const char* src, const char* name)
{
    GLuint s = glCreateShader(type); glShaderSource(s, 1, &src, nullptr); glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[768] = ""; glGetShaderInfoLog(s, sizeof(log), nullptr, log); PS_Log("deferred: %s compile failed: %s", name, log); glDeleteShader(s); return false; }
    out = s; return true;
}

bool Get(Prog& p)
{
    if (p.id) return true;
    if (p.tried) return false;
    p.tried = true;
    std::string vs, fs;
    if (!GLSL_Source("vertex/shadow_screen.glsl", vs) || !GLSL_Source(p.fs, fs)) { PS_Log("deferred: %s is not built into the mod", p.fs); return false; }
    GLuint v = 0, f = 0;
    if (!Compile(v, GL_VERTEX_SHADER, vs.c_str(), "shadow_screen.glsl")) return false;
    if (!Compile(f, GL_FRAGMENT_SHADER, fs.c_str(), p.fs)) { glDeleteShader(v); GLSL_MarkFailed(p.fs, "did not compile - see propergfx_log.txt"); return false; }
    GLuint id = glCreateProgram(); glAttachShader(id, v); glAttachShader(id, f); glLinkProgram(id); glDeleteShader(v); glDeleteShader(f);
    GLint ok = 0; glGetProgramiv(id, GL_LINK_STATUS, &ok);
    if (!ok) { char log[512] = ""; glGetProgramInfoLog(id, sizeof(log), nullptr, log); PS_Log("deferred: %s link failed: %s", p.fs, log); glDeleteProgram(id); GLSL_MarkFailed(p.fs, "link failed"); return false; }
    p.id = id; return true;
}

inline GLint U(GLuint p, const char* n) { return glGetUniformLocation(p, n); }
inline void S1i(GLuint p, const char* n, int v) { GLint l = U(p, n); if (l >= 0) glUniform1i(l, v); }
inline void S2(GLuint p, const char* n, float a, float b) { GLint l = U(p, n); if (l >= 0) glUniform2f(l, a, b); }
inline void S4(GLuint p, const char* n, float a, float b, float c, float d) { GLint l = U(p, n); if (l >= 0) glUniform4f(l, a, b, c, d); }
inline void SM(GLuint p, const char* n, const float* m) { GLint l = U(p, n); if (l >= 0) glUniformMatrix4fv(l, 1, GL_FALSE, m); }
inline void Tex(int unit, GLuint t) { glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_2D, t); }
inline void Draw() { glDrawArrays(GL_TRIANGLES, 0, 3); ++D.st.passes; }

void FreeRT(RT& r) { if (r.fbo) glDeleteFramebuffers(1, &r.fbo); if (r.tex) glDeleteTextures(1, &r.tex); r = RT(); }
bool MakeRT(RT& r, int w, int h, bool linear = true)
{
    if (r.fbo && r.w == w && r.h == h) return true;
    FreeRT(r);
    glGenTextures(1, &r.tex); Tex(0, r.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &r.fbo); glBindFramebuffer(GL_FRAMEBUFFER, r.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r.tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { FreeRT(r); return false; }
    r.w = w; r.h = h; return true;
}
void Target(const RT& r) { glBindFramebuffer(GL_FRAMEBUFFER, r.fbo); glViewport(0, 0, r.w, r.h); }

// copy of the bound framebuffer's colour (the passes read it while writing their own targets: no feedback loop)
bool CopyColour(GLuint fbo, int w, int h)
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (!D.copyTex || D.cw != w || D.ch != h)
    {
        if (D.copyTex) glDeleteTextures(1, &D.copyTex);
        GLint ab = 0; glGetIntegerv(GL_ALPHA_BITS, &ab);
        D.copyFmt = ab > 0 ? GL_RGBA : GL_RGB;
        glGenTextures(1, &D.copyTex); Tex(0, D.copyTex);
        glTexImage2D(GL_TEXTURE_2D, 0, D.copyFmt, w, h, 0, D.copyFmt, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        D.cw = w; D.ch = h;
    }
    Tex(0, D.copyTex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
    return true;
}
// our RT -> the game's target (colour only: its depth texture is never sampled while bound for drawing)
void BlitTo(const RT& src, GLuint dst, int w, int h)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, src.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst);
    glBlitFramebuffer(0, 0, src.w, src.h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, dst);
}

float Smooth(float a, float b, float x) { float t = x < a ? 0.f : (x > b ? 1.f : (x - a) / (b - a)); return t * t * (3.f - 2.f * t); }
float Mixf(float a, float b, float t) { return a + (b - a) * t; }

// colours of the analytic environment from the sun height (sky / horizon / ground ambient, sun colour)
struct Env { float sky[3], hor[3], gnd[3], sun[3], sunH; };
Env MakeEnv(const float sv[3], float night)
{
    Env e;
    const float z = sv[2];
    const float day = Smooth(-0.12f, 0.25f, z) * (1.f - night * 0.6f);
    const float dusk = fmaxf(0.f, 1.f - fabsf(z - 0.06f) / 0.22f);
    const float skyD[3] = { 0.30f, 0.50f, 0.95f }, skyN[3] = { 0.03f, 0.05f, 0.12f };
    const float horD[3] = { 0.80f, 0.80f, 0.85f }, horN[3] = { 0.06f, 0.07f, 0.12f }, horS[3] = { 1.00f, 0.55f, 0.30f };
    const float gndD[3] = { 0.36f, 0.31f, 0.25f }, gndN[3] = { 0.02f, 0.02f, 0.03f };
    for (int i = 0; i < 3; ++i)
    {
        e.sky[i] = Mixf(skyN[i], skyD[i], day);
        e.hor[i] = Mixf(Mixf(horN[i], horD[i], day), horS[i], dusk * 0.7f);
        e.gnd[i] = Mixf(gndN[i], gndD[i], day);
    }
    const float sunLo[3] = { 1.0f, 0.50f, 0.25f }, sunHi[3] = { 1.0f, 0.95f, 0.88f };
    const float k = Smooth(0.0f, 0.4f, z);
    e.sunH = Smooth(-0.04f, 0.10f, z) * (1.f - night * 0.8f);
    for (int i = 0; i < 3; ++i) e.sun[i] = Mixf(sunLo[i], sunHi[i], k);
    return e;
}

void MulD(const double* a, const double* b, double* o) { for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) { double s = 0; for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k]; o[c * 4 + r] = s; } }

void Common(GLuint p, const PSFrameCam& c, const Env& e, int w, int h)
{
    S1i(p, "uDepth", 0);
    SM(p, "uIVP", c.ivp); SM(p, "uVP", c.vp);
    S4(p, "uCam", c.cam[0], c.cam[1], c.cam[2], fmodf(D.f.time, 3600.f));
    S4(p, "uSunD", D.f.sun[0], D.f.sun[1], D.f.sun[2], e.sunH);
    S4(p, "uRes", (float)w, (float)h, 1.f / w, 1.f / h);
    S4(p, "uLin", c.proj[10], c.proj[14], 3000.f, D.frameNo);
}

bool CheckES3()
{
    if (!D.checked)
    {
        D.checked = true;
        const char* v = (const char*)glGetString(GL_VERSION);
        D.es3 = v && strstr(v, "OpenGL ES 3") != nullptr;
        D.st.es3 = D.es3;
        if (!D.es3) Note("deferred lighting needs OpenGL ES 3.0");
    }
    return D.es3;
}

void GLDefaults()
{
    glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST);
    glDisable(GL_DITHER); glDisable(GL_POLYGON_OFFSET_FILL); glDepthMask(GL_FALSE); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    for (int i = 0; i < 8; ++i) glDisableVertexAttribArray(i);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

} // namespace

bool Deferred_Wanted(const PSSettings& s)
{
    if (!s.enabled) return false;
    const bool lit = s.deferredOn && (s.aoOn || s.ambientOn || s.skylightOn || s.contactOn || s.volFogOn || s.headlightsOn || s.ssrOn || s.rainSurfOn ||
                                      s.rainFallOn || s.causticsOn || s.pbrSkyOn || s.cloudsOn || s.auroraOn);
    const bool dof = s.postFx && s.tiltShift && s.dofMode == 2;
    return lit || dof || Deferred_PostWanted(s);
}
bool Deferred_PostWanted(const PSSettings& s) { return s.enabled && (s.taaOn || (s.fsrOn && s.fsrSharpness > 0.01f)); }
void Deferred_SetFrame(const PSDeferredFrame& f) { D.f = f; }
GLuint Deferred_CocTexture() { return D.cocOut; }
const PSDeferredStats& Deferred_Stats() { return D.st; }

void Deferred_AfterScene(const PSSettings& s, int level)
{
    D.cocOut = 0; D.st.passes = 0; D.st.ao = D.st.vol = D.st.composite = D.st.coc = false;
    if (!s.enabled || !CheckES3()) return;
    const bool dof = s.postFx && s.tiltShift && s.dofMode == 2;
    if (!s.deferredOn && !dof) return;
    PSFrameCam c;
    if (!Shadow_GetFrameCamera(c)) { ++D.st.noCamera; Note("waiting for the game camera / scene depth (gameplay only)"); return; }
    const int w = c.w, h = c.h;
    D.frameNo = fmodf(D.frameNo + 1.f, 64.f);
    ++D.st.frames;
    const Env e = MakeEnv(D.f.sun, D.f.night);
    GLDefaults();

    const float wet = s.rainSurfOn ? fminf(fmaxf(D.f.wet, 0.f), 1.f) : 0.f;
    const bool wantAO   = s.deferredOn && s.aoOn && s.aoIntensity > 0.01f && level < 3;
    const bool wantVol  = s.deferredOn && ((s.volFogOn && s.volFogDensity.v > 0.001f) || (s.headlightsOn && D.f.headlights)) && level < 3;
    const bool wantSky  = s.deferredOn && ((s.pbrSkyOn && s.pbrSkyBlend > 0.01f) || s.cloudsOn || (s.auroraOn && D.f.night > 0.05f));
    const bool wantComp = s.deferredOn && (wantAO || wantVol || wantSky || s.ambientOn || s.skylightOn || s.contactOn || s.ssrOn || wet > 0.01f ||
                                           (s.rainFallOn && D.f.rain > 0.01f) || s.causticsOn);

    if (wantComp)
    {
        if (!Get(D.comp)) { Note("def_composite.glsl failed - see propergfx_log.txt"); return; }
        CopyColour((GLuint)c.fbo, w, h);
        const int sc = (s.deferredHalf || level >= 1) ? 2 : 1;
        const int hw = (w + sc - 1) / sc, hh = (h + sc - 1) / sc;
        // ---- AO / GI
        bool aoOk = false;
        if (wantAO && Get(D.ao) && Get(D.bil) && MakeRT(D.aoA, hw, hh) && MakeRT(D.aoB, hw, hh))
        {
            const GLuint p = D.ao.id;
            Target(D.aoA); glUseProgram(p); Common(p, c, e, w, h);
            Tex(0, c.depthTex); Tex(1, D.copyTex); S1i(p, "uScene", 1);
            const int mode = (s.aoMode == AO_SSGI && level >= 2) ? AO_HBAO : s.aoMode;
            S4(p, "uP", s.aoRadius, s.aoIntensity, (float)mode, c.proj[5] * 0.5f);
            Draw();
            const GLuint b = D.bil.id;
            glUseProgram(b); Common(b, c, e, w, h); S1i(b, "uTex", 1);
            Target(D.aoB); Tex(1, D.aoA.tex); S2(b, "uDir", 1.f / hw, 0.f); Draw();
            Target(D.aoA); Tex(1, D.aoB.tex); S2(b, "uDir", 0.f, 1.f / hh); Draw();
            aoOk = true; D.st.ao = true;
        }
        // ---- volumetric fog / god rays / headlights
        bool volOk = false;
        if (wantVol && Get(D.vol) && Get(D.bil))
        {
            const int vw = (w + 1) / 2, vh = (h + 1) / 2;
            if (MakeRT(D.volA, vw, vh) && MakeRT(D.volB, vw, vh))
            {
                const GLuint p = D.vol.id;
                Target(D.volA); glUseProgram(p); Common(p, c, e, w, h);
                Tex(0, c.depthTex); Tex(2, c.mapTex ? c.mapTex : c.depthTex); S1i(p, "uMap", 2);
                float L[16]; if (c.mapOk) memcpy(L, c.lvp, sizeof(L)); else { memset(L, 0, sizeof(L)); L[0] = L[5] = L[10] = L[15] = 1.f; }
                SM(p, "uL", L);
                const float dens = s.volFogOn ? s.volFogDensity.v : 0.02f;
                S4(p, "uV", dens * (1.f + D.f.rain * 1.5f), s.volFogFalloff, s.volGodrays, c.mapOk ? 1.f : 0.f);
                S4(p, "uSunC", e.sun[0] * 1.2f, e.sun[1] * 1.2f, e.sun[2] * 1.2f, 260.f);
                S4(p, "uAmb", e.hor[0] * 0.35f, e.hor[1] * 0.35f, e.hor[2] * 0.35f, s.headlightsOn && D.f.headlights ? s.headlightStrength * D.f.night : 0.f);
                const bool hl = s.headlightsOn && D.f.headlights && D.f.night > 0.2f;
                S4(p, "uHL0", D.f.hl0[0] - c.cam[0], D.f.hl0[1] - c.cam[1], D.f.hl0[2] - c.cam[2], hl ? 1.f : 0.f);
                S4(p, "uHL1", D.f.hl1[0] - c.cam[0], D.f.hl1[1] - c.cam[1], D.f.hl1[2] - c.cam[2], hl ? 1.f : 0.f);
                S4(p, "uHLD", D.f.hlDir[0], D.f.hlDir[1], D.f.hlDir[2], 0.f);
                Draw();
                if (c.mapOk) Tex(2, 0);
                const GLuint b = D.bil.id;
                glUseProgram(b); Common(b, c, e, w, h); S1i(b, "uTex", 1);
                Target(D.volB); Tex(1, D.volA.tex); S2(b, "uDir", 1.f / vw, 0.f); Draw();
                Target(D.volA); Tex(1, D.volB.tex); S2(b, "uDir", 0.f, 1.f / vh); Draw();
                volOk = true; D.st.vol = true;
            }
        }
        // ---- composite -> our target -> blit into the game's scene target
        if (MakeRT(D.compRT, w, h, false))
        {
            const GLuint p = D.comp.id;
            Target(D.compRT); glUseProgram(p); Common(p, c, e, w, h);
            Tex(0, c.depthTex); Tex(1, D.copyTex); Tex(2, aoOk ? D.aoA.tex : D.copyTex); Tex(3, volOk ? D.volA.tex : D.copyTex);
            S1i(p, "uScene", 1); S1i(p, "uAO", 2); S1i(p, "uVol", 3);
            S4(p, "uAmbS", e.sky[0], e.sky[1], e.sky[2], s.ambientOn ? s.ambientStrength : 0.f);
            S4(p, "uAmbH", e.hor[0], e.hor[1], e.hor[2], s.skylightOn ? s.skylightStrength : 0.f);
            S4(p, "uAmbG", e.gnd[0], e.gnd[1], e.gnd[2], aoOk ? 1.f : 0.f);
            S4(p, "uSunC", e.sun[0], e.sun[1], e.sun[2], (s.contactOn && level < 2) ? s.contactStrength * (c.mapOk ? 1.f : 0.6f) : 0.f);
            S4(p, "uA", (aoOk && s.aoMode == AO_SSGI) ? s.giStrength : 0.f, s.contactLength, s.ssrOn && level < 2 ? s.ssrStrength : 0.f, s.ssrDry);
            S4(p, "uW", wet, s.puddles, s.ripples, s.causticsOn ? s.causticsStrength : 0.f);
            S4(p, "uS", s.pbrSkyOn ? s.pbrSkyBlend : 0.f, s.cloudCoverage, s.cloudsOn ? s.cloudDensity : 0.f, s.auroraOn ? s.auroraStrength : 0.f);
            S4(p, "uS2", s.cloudSpeed, D.f.night, s.rainFallOn ? s.rainFall : 0.f, D.f.underwater);
            S4(p, "uOn", volOk ? 1.f : 0.f, level >= 1 ? 10.f : 18.f, aoOk ? 1.f : 0.f, fminf(fmaxf(D.f.rain, 0.f), 1.f));
            Draw();
            BlitTo(D.compRT, (GLuint)c.fbo, w, h);
            D.st.composite = true;
        }
    }

    // ---- [DepthOfField] Adaptive: auto focus + circle of confusion (read by the post pass this frame)
    if (dof && Get(D.focus) && Get(D.coc) && MakeRT(D.focusRT[0], 1, 1, false) && MakeRT(D.focusRT[1], 1, 1, false))
    {
        const int qw = (w + 3) / 4, qh = (h + 3) / 4;
        if (MakeRT(D.cocRT, qw, qh))
        {
            RT& cur = D.focusRT[D.focusIdx ^ 1]; RT& prev = D.focusRT[D.focusIdx];
            GLuint p = D.focus.id;
            Target(cur); glUseProgram(p); Common(p, c, e, w, h);
            Tex(0, c.depthTex); Tex(1, prev.tex); S1i(p, "uPrev", 1);
            const float adapt = fminf(fmaxf(1.f - expf(-D.f.dt * 3.0f), 0.02f), 1.f);
            S4(p, "uP", adapt, D.focusReset ? 1.f : 0.f, 0.f, 0.f);
            Draw();
            D.focusIdx ^= 1; D.focusReset = false;
            p = D.coc.id;
            Target(D.cocRT); glUseProgram(p); Common(p, c, e, w, h);
            Tex(0, c.depthTex); Tex(1, cur.tex); S1i(p, "uFocus", 1);
            // sharp zone grows with the focus distance; Strength = how much blur, FocusWidth = how deep the sharp zone is
            S4(p, "uP", 2.f + s.tiltWidth * 20.f, 1.f, 0.6f, 0.35f + s.tiltStrength * 0.65f);
            Draw();
            D.cocOut = D.cocRT.tex; D.st.coc = true;
        }
    }
    else D.focusReset = true;
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)c.fbo);
    D.st.ready = true;
    Note(wantComp ? "ok" : (dof ? "ok (adaptive depth of field only)" : "idle"));
}

void Deferred_PostAA(const PSSettings& s, GLuint fbo, int w, int h)
{
    D.st.taa = D.st.fsr = false;
    if (!Deferred_PostWanted(s) || !CheckES3() || w < 16 || h < 16) return;
    GLDefaults();
    PSFrameCam c; const bool cam = Shadow_GetFrameCamera(c) && c.w == w && c.h == h;
    const Env e = MakeEnv(D.f.sun, D.f.night);
    if (s.taaOn && cam && Get(D.taa) && MakeRT(D.hist[0], w, h) && MakeRT(D.hist[1], w, h))
    {
        // previous camera: ndc now -> (world - camera now) -> world -> previous clip
        double PV[16], P[16], V[16];
        for (int i = 0; i < 16; ++i) { P[i] = c.proj[i]; V[i] = c.view[i]; }
        MulD(P, V, PV);
        const float jump = sqrtf((c.cam[0] - D.prevCam[0]) * (c.cam[0] - D.prevCam[0]) + (c.cam[1] - D.prevCam[1]) * (c.cam[1] - D.prevCam[1]) +
                                 (c.cam[2] - D.prevCam[2]) * (c.cam[2] - D.prevCam[2]));
        const bool reset = D.histReset || !D.havePrev || jump > 6.f;
        float rp[16];
        {
            double T[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}, A[16], I[16], M[16];
            T[12] = c.cam[0]; T[13] = c.cam[1]; T[14] = c.cam[2];
            MulD(D.havePrev ? D.prevPV : PV, T, A);
            for (int i = 0; i < 16; ++i) I[i] = c.ivp[i];
            MulD(A, I, M);
            for (int i = 0; i < 16; ++i) rp[i] = (float)M[i];
        }
        CopyColour(fbo, w, h);
        RT& dst = D.hist[D.histIdx ^ 1]; RT& src = D.hist[D.histIdx];
        const GLuint p = D.taa.id;
        Target(dst); glUseProgram(p); Common(p, c, e, w, h);
        Tex(0, c.depthTex); Tex(1, D.copyTex); Tex(2, src.tex); S1i(p, "uCur", 1); S1i(p, "uHist", 2);
        SM(p, "uReproj", rp);
        S4(p, "uP", s.taaBlend, reset ? 1.f : 0.f, 0.f, 0.f);
        Draw();
        BlitTo(dst, fbo, w, h);
        D.histIdx ^= 1; D.histReset = false;
        memcpy(D.prevPV, PV, sizeof(PV)); memcpy(D.prevCam, c.cam, sizeof(D.prevCam)); D.havePrev = true;
        D.st.taa = true;
    }
    else D.histReset = true;
    if (s.fsrOn && s.fsrSharpness > 0.01f && Get(D.fsr) && MakeRT(D.post, w, h, false))
    {
        CopyColour(fbo, w, h);
        const GLuint p = D.fsr.id;
        Target(D.post); glUseProgram(p);
        Tex(1, D.copyTex); S1i(p, "uCur", 1);
        S4(p, "uRes", (float)w, (float)h, 1.f / w, 1.f / h);
        S4(p, "uP", s.fsrSharpness, 0.f, 0.f, 0.f);
        Draw();
        BlitTo(D.post, fbo, w, h);
        D.st.fsr = true;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
}

void Deferred_ContextLost()
{
    PSDeferredFrame f = D.f;
    D = DState(); D.f = f;
    Note("context lost - rebuilding");
}

void Deferred_Shutdown()
{
    Prog* ps[] = { &D.ao, &D.bil, &D.vol, &D.comp, &D.focus, &D.coc, &D.taa, &D.fsr };
    for (Prog* p : ps) if (p->id) glDeleteProgram(p->id);
    RT* rs[] = { &D.aoA, &D.aoB, &D.volA, &D.volB, &D.compRT, &D.focusRT[0], &D.focusRT[1], &D.cocRT, &D.hist[0], &D.hist[1], &D.post };
    for (RT* r : rs) FreeRT(*r);
    if (D.copyTex) glDeleteTextures(1, &D.copyTex);
    D = DState();
}
