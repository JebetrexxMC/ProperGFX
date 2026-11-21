// TyFMCGFX 1.6.0 - Firestorm "Preferences" look, shared by src32 (GTA SA 2.00) and src64 (GTA SA 2.10).
// Matches Research/images (15).jpeg: dark grey panels, a "Preferences" title row with ? _ x, a "Search Settings" row,
// a category list whose selected entry is filled green, green tabs / buttons / drop-downs, green check marks and
// thin green slider tracks with a round knob and the value printed on the right.
// Header-only (inline): both .so files include it, no extra source file in build.sh.
#pragma once
#include "imgui.h"
#include "imgui_internal.h"      // ImGui::MarkItemEdited for the custom-drawn slider
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace fsui {

// ---------------------------------------------------------------- palette (sampled from the Firestorm screenshot)
inline ImVec4 C(float r, float g, float b, float a = 1.f) { return ImVec4(r, g, b, a); }
inline ImVec4 Green()      { return C(0.235f, 0.478f, 0.196f); }    // selected category / active tab / buttons  (~#3c7a32)
inline ImVec4 GreenHi()    { return C(0.310f, 0.600f, 0.255f); }    // hovered                                  (~#4f9941)
inline ImVec4 GreenLo()    { return C(0.165f, 0.345f, 0.145f); }    // pressed / combo body                     (~#2a5825)
inline ImVec4 GreenCheck() { return C(0.470f, 0.880f, 0.330f); }    // check marks                              (~#78e054)
inline ImVec4 GreenTrack() { return C(0.200f, 0.520f, 0.170f); }    // slider track line
inline ImVec4 GreenKnob()  { return C(0.330f, 0.700f, 0.260f); }    // slider knob
inline ImVec4 SideIdle()   { return C(0.150f, 0.190f, 0.150f); }    // unselected category entries (grey with a green tint)
inline ImVec4 Panel()      { return C(0.125f, 0.125f, 0.125f, 0.975f); }
inline ImVec4 Inset()      { return C(0.090f, 0.090f, 0.090f, 0.96f); }
inline ImVec4 Field()      { return C(0.055f, 0.055f, 0.055f, 1.f); }
inline ImU32  U(const ImVec4& v, float a = 1.f) { return ImGui::GetColorU32(ImVec4(v.x, v.y, v.z, v.w * a)); }

inline void ApplyStyle(float sc)
{
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    ImGui::StyleColorsDark();
    st.WindowRounding = 0; st.FrameRounding = 2; st.GrabRounding = 10; st.ScrollbarRounding = 2; st.TabRounding = 0; st.ChildRounding = 0; st.PopupRounding = 2;
    st.WindowBorderSize = 1; st.FrameBorderSize = 1; st.PopupBorderSize = 1; st.ChildBorderSize = 1;
    st.WindowPadding = ImVec2(8, 6); st.FramePadding = ImVec2(8, 4); st.ItemSpacing = ImVec2(8, 5);
    st.ItemInnerSpacing = ImVec2(7, 5); st.GrabMinSize = 18; st.ScrollbarSize = 24; st.IndentSpacing = 16;
    st.SeparatorTextBorderSize = 1; st.SeparatorTextPadding = ImVec2(12, 3); st.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    st.WindowTitleAlign = ImVec2(0.0f, 0.5f); st.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    st.ScaleAllSizes(sc);
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]                 = C(0.90f, 0.90f, 0.90f);
    c[ImGuiCol_TextDisabled]         = C(0.52f, 0.52f, 0.52f);
    c[ImGuiCol_WindowBg]             = Panel();
    c[ImGuiCol_ChildBg]              = Inset();
    c[ImGuiCol_PopupBg]              = C(0.11f, 0.11f, 0.11f, 0.99f);
    c[ImGuiCol_Border]               = C(0.32f, 0.32f, 0.32f, 0.85f);
    c[ImGuiCol_BorderShadow]         = C(0, 0, 0, 0);
    c[ImGuiCol_TitleBg]              = C(0.16f, 0.16f, 0.16f);
    c[ImGuiCol_TitleBgActive]        = C(0.16f, 0.16f, 0.16f);
    c[ImGuiCol_TitleBgCollapsed]     = C(0.16f, 0.16f, 0.16f);
    c[ImGuiCol_FrameBg]              = Field();                     // check boxes / text fields: near black
    c[ImGuiCol_FrameBgHovered]       = C(0.10f, 0.13f, 0.10f);
    c[ImGuiCol_FrameBgActive]        = C(0.12f, 0.17f, 0.12f);
    c[ImGuiCol_SliderGrab]           = GreenKnob();
    c[ImGuiCol_SliderGrabActive]     = GreenCheck();
    c[ImGuiCol_CheckMark]            = GreenCheck();
    c[ImGuiCol_Button]               = Green();
    c[ImGuiCol_ButtonHovered]        = GreenHi();
    c[ImGuiCol_ButtonActive]         = GreenLo();
    c[ImGuiCol_Header]               = C(0.17f, 0.22f, 0.17f);
    c[ImGuiCol_HeaderHovered]        = C(0.21f, 0.32f, 0.20f);
    c[ImGuiCol_HeaderActive]         = Green();
    c[ImGuiCol_Separator]            = C(0.30f, 0.30f, 0.30f);
    c[ImGuiCol_SeparatorHovered]     = GreenHi();
    c[ImGuiCol_SeparatorActive]      = GreenCheck();
    c[ImGuiCol_ScrollbarBg]          = C(0.07f, 0.07f, 0.07f, 0.9f);
    c[ImGuiCol_ScrollbarGrab]        = C(0.28f, 0.28f, 0.28f);
    c[ImGuiCol_ScrollbarGrabHovered] = C(0.36f, 0.44f, 0.35f);
    c[ImGuiCol_ScrollbarGrabActive]  = Green();
    c[ImGuiCol_ResizeGrip]           = C(0.24f, 0.48f, 0.20f, 0.35f);
    c[ImGuiCol_ResizeGripHovered]    = C(0.31f, 0.60f, 0.26f, 0.70f);
    c[ImGuiCol_ResizeGripActive]     = GreenCheck();
    c[ImGuiCol_Tab]                  = C(0.16f, 0.16f, 0.16f);
    c[ImGuiCol_TabHovered]           = GreenHi();
    c[ImGuiCol_TabActive]            = Green();
    c[ImGuiCol_TableBorderLight]     = C(0.22f, 0.22f, 0.22f);
    c[ImGuiCol_TableBorderStrong]    = C(0.30f, 0.30f, 0.30f);
    c[ImGuiCol_NavHighlight]         = GreenCheck();
}

// ---------------------------------------------------------------- buttons
// Category list entry: grey-green, selected = solid green (the "Graphics" entry in the screenshot)
inline bool SideButton(const char* l, bool sel, float w)
{
    ImGui::PushStyleColor(ImGuiCol_Button, sel ? Green() : SideIdle());
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, sel ? GreenHi() : C(0.19f, 0.27f, 0.18f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, GreenLo());
    ImGui::PushStyleColor(ImGuiCol_Text, sel ? C(1, 1, 1) : C(0.86f, 0.86f, 0.86f));
    ImGui::PushStyleColor(ImGuiCol_Border, C(0.05f, 0.05f, 0.05f, 0.9f));
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
    bool r = ImGui::Button(l, ImVec2(w, 0));
    ImGui::PopStyleVar(2); ImGui::PopStyleColor(5);
    return r;
}
// Tab row (General / Hardware Settings / Rendering / Depth of Field): active tab green, the others flat grey
inline bool TabButton(const char* l, bool sel)
{
    ImGui::PushStyleColor(ImGuiCol_Button, sel ? Green() : C(0.165f, 0.165f, 0.165f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, sel ? GreenHi() : C(0.22f, 0.26f, 0.21f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, GreenLo());
    ImGui::PushStyleColor(ImGuiCol_Text, sel ? C(1, 1, 1) : C(0.82f, 0.82f, 0.82f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
    bool r = ImGui::Button(l);
    ImGui::PopStyleVar(); ImGui::PopStyleColor(4);
    return r;
}
// Plain push button (Save / Load / Delete / OK / Cancel): green with a dark outline
inline bool Button(const char* l, const ImVec2& sz = ImVec2(0, 0))
{
    ImGui::PushStyleColor(ImGuiCol_Border, C(0.04f, 0.08f, 0.04f, 1.f));
    bool r = ImGui::Button(l, sz);
    ImGui::PopStyleColor();
    return r;
}
// small flat glyph button for the title row ( ?  _  x )
inline bool GlyphButton(const char* l)
{
    ImGui::PushStyleColor(ImGuiCol_Button, C(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, C(0.30f, 0.30f, 0.30f, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Green());
    ImGui::PushStyleColor(ImGuiCol_Border, C(0, 0, 0, 0));
    bool r = ImGui::Button(l, ImVec2(ImGui::GetFrameHeight() * 0.95f, ImGui::GetFrameHeight() * 0.85f));
    ImGui::PopStyleColor(4);
    return r;
}

// ---------------------------------------------------------------- slider: thin green line + round knob (no number on the knob)
// Returns true while the value changes; marks the item edited so IsItemEdited() works for the callers.
inline bool Track(const char* id, float* v, float a, float b, float width, bool snapInt = false)
{
    ImGuiIO& io = ImGui::GetIO();
    const float h = ImGui::GetFrameHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(width, h));
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    const float r = h * 0.30f, x0 = p.x + r + 1.f, x1 = p.x + width - r - 1.f;
    bool changed = false;
    if (active && x1 > x0)
    {
        float t = (io.MousePos.x - x0) / (x1 - x0); t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
        float nv = a + (b - a) * t;
        if (snapInt) nv = floorf(nv + 0.5f);
        if (nv != *v) { *v = nv; changed = true; ImGui::MarkItemEdited(ImGui::GetItemID()); }
    }
    const bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
    const float alpha = disabled ? 0.45f : 1.f;
    float t = (b != a) ? (*v - a) / (b - a) : 0.f; t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    const float cy = p.y + h * 0.5f, kx = x0 + (x1 - x0) * t;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float th = fmaxf(2.f, h * 0.09f);
    dl->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), U(C(0.10f, 0.22f, 0.09f), alpha), th + 2.f);   // dark rim
    dl->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), U(GreenTrack(), alpha), th);
    dl->AddCircleFilled(ImVec2(kx, cy), r, U(active ? GreenCheck() : (hovered ? GreenHi() : GreenKnob()), alpha), 20);
    dl->AddCircle(ImVec2(kx, cy), r, U(C(0.62f, 0.92f, 0.52f), alpha * 0.9f), 20, fmaxf(1.f, h * 0.05f));
    return changed;
}

// ---------------------------------------------------------------- search
inline bool Match(const char* label, const char* q)
{
    if (!q || !q[0]) return true;
    if (!label) return false;
    const size_t n = strlen(q);
    for (const char* s = label; *s; ++s)
    {
        size_t i = 0;
        while (i < n && s[i] && tolower((unsigned char)s[i]) == tolower((unsigned char)q[i])) ++i;
        if (i == n) return true;
    }
    return false;
}
// "Search Settings" field. Android has no IME for ImGui here, so a tap opens the built-in keyboard (Keyboard() below).
inline bool SearchField(const char* buf, bool* kbOpen, float width)
{
    const float h = ImGui::GetFrameHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool tap = ImGui::InvisibleButton("##search", ImVec2(width, h));
    if (tap) *kbOpen = !*kbOpen;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), U(Field()));
    dl->AddRect(p, ImVec2(p.x + width, p.y + h), U(*kbOpen ? GreenHi() : C(0.30f, 0.30f, 0.30f)));
    // magnifier glyph
    const float r = h * 0.20f; ImVec2 mc(p.x + h * 0.45f, p.y + h * 0.45f);
    dl->AddCircle(mc, r, U(C(0.75f, 0.75f, 0.75f)), 14, fmaxf(1.2f, h * 0.06f));
    dl->AddLine(ImVec2(mc.x + r * 0.7f, mc.y + r * 0.7f), ImVec2(mc.x + r * 1.7f, mc.y + r * 1.7f), U(C(0.75f, 0.75f, 0.75f)), fmaxf(1.5f, h * 0.08f));
    const ImGuiStyle& st = ImGui::GetStyle();
    ImVec2 tp(p.x + h * 0.95f, p.y + (h - ImGui::GetFontSize()) * 0.5f);
    if (buf[0]) dl->AddText(tp, U(C(0.95f, 0.95f, 0.95f)), buf);
    else dl->AddText(tp, U(C(0.55f, 0.55f, 0.55f)), "Search Settings");
    if (*kbOpen && fmodf((float)ImGui::GetTime(), 1.f) < 0.55f)
    {   // caret
        float cx = tp.x + ImGui::CalcTextSize(buf).x + 2.f;
        dl->AddLine(ImVec2(cx, p.y + st.FramePadding.y), ImVec2(cx, p.y + h - st.FramePadding.y), U(GreenCheck()), 1.5f);
    }
    return tap;
}
// on-screen QWERTY keyboard; returns true when the text changed
inline bool Keyboard(char* buf, size_t cap, bool* kbOpen)
{
    static const char* rows[3] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
    bool changed = false;
    const float avail = ImGui::GetContentRegionAvail().x, sp = ImGui::GetStyle().ItemSpacing.x * 0.5f;
    const float kw = fminf((avail - sp * 9.f) / 10.f, ImGui::GetFontSize() * 3.2f), kh = ImGui::GetFrameHeight() * 1.15f;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(sp, sp));
    ImGui::PushStyleColor(ImGuiCol_Button, C(0.18f, 0.18f, 0.18f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GreenHi());
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Green());
    for (int r = 0; r < 3; ++r)
    {
        const int n = (int)strlen(rows[r]);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (10 - n) * (kw + sp) * 0.5f);
        for (int i = 0; i < n; ++i)
        {
            char k[2] = { rows[r][i], 0 };
            ImGui::PushID(r * 16 + i);
            if (ImGui::Button(k, ImVec2(kw, kh)))
            {
                size_t l = strlen(buf);
                if (l + 1 < cap) { buf[l] = (char)tolower((unsigned char)k[0]); buf[l + 1] = 0; changed = true; }
            }
            ImGui::PopID();
            if (i < n - 1) ImGui::SameLine();
        }
    }
    ImGui::PopStyleColor(3);
    const float bw = (kw * 10.f + sp * 9.f - sp * 3.f) / 4.f;
    if (Button("Space", ImVec2(bw, kh))) { size_t l = strlen(buf); if (l + 1 < cap && l > 0) { buf[l] = ' '; buf[l + 1] = 0; changed = true; } }
    ImGui::SameLine(); if (Button("<- Back", ImVec2(bw, kh))) { size_t l = strlen(buf); if (l) { buf[l - 1] = 0; changed = true; } }
    ImGui::SameLine(); if (Button("Clear", ImVec2(bw, kh))) { if (buf[0]) { buf[0] = 0; changed = true; } }
    ImGui::SameLine(); if (Button("Done", ImVec2(bw, kh))) *kbOpen = false;
    ImGui::PopStyleVar();
    return changed;
}

// ---------------------------------------------------------------- reset glyph (circular arrow, right of the quality slider)
inline bool ResetGlyph(const char* id)
{
    const float h = ImGui::GetFrameHeight();
    bool r = Button(id, ImVec2(h * 1.2f, h));
    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rr = h * 0.26f, PI = 3.14159265f;
    dl->PathArcTo(c, rr, -PI * 0.35f, PI * 1.45f, 20); dl->PathStroke(U(C(1, 1, 1)), 0, fmaxf(1.5f, h * 0.08f));
    ImVec2 tip(c.x + cosf(-PI * 0.35f) * rr, c.y + sinf(-PI * 0.35f) * rr);
    dl->AddTriangleFilled(ImVec2(tip.x - rr * 0.55f, tip.y - rr * 0.25f), ImVec2(tip.x + rr * 0.35f, tip.y - rr * 0.55f), ImVec2(tip.x + rr * 0.15f, tip.y + rr * 0.45f), U(C(1, 1, 1)));
    return r;
}

// ---------------------------------------------------------------- status-bar pill (the floating button; the screenshot's arrow points at the
// viewer's status bar: clock, a monitor icon and the frame rate on a black strip with a red end cap)
struct Pill { ImVec2 mn, mx; };
inline Pill MeasurePill(float side, const char* clock, const char* fps, bool collapsed)
{
    Pill p; p.mn = ImVec2(0, 0);
    if (collapsed) { p.mx = ImVec2(side, side); return p; }
    const float pad = side * 0.22f;
    float w = pad + ImGui::CalcTextSize(clock).x + pad + side * 0.85f + pad + ImGui::CalcTextSize(fps).x + pad + side * 0.12f;
    p.mx = ImVec2(w, side);
    return p;
}
// draws text parts; the caller draws the monitor/mod icon at the returned centre
inline ImVec2 DrawPill(ImDrawList* dl, ImVec2 mn, ImVec2 mx, const char* clock, const char* fps, bool collapsed, bool active, float a)
{
    const float side = mx.y - mn.y, pad = side * 0.22f;
    ImU32 bg = U(active ? C(0.10f, 0.20f, 0.09f) : C(0.f, 0.f, 0.f), a * (collapsed ? 0.70f : 0.88f));
    dl->AddRectFilled(mn, mx, bg, side * 0.10f);
    dl->AddRect(mn, mx, U(active ? GreenCheck() : C(0.35f, 0.35f, 0.35f), a * 0.9f), side * 0.10f, 0, fmaxf(1.f, side * 0.04f));
    if (collapsed) return ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    const float fy = mn.y + (side - ImGui::GetFontSize()) * 0.5f;
    float x = mn.x + pad;
    dl->AddText(ImVec2(x, fy), U(C(1, 1, 1), a), clock);
    x += ImGui::CalcTextSize(clock).x + pad;
    ImVec2 ic(x + side * 0.42f, (mn.y + mx.y) * 0.5f);
    x += side * 0.85f + pad;
    dl->AddText(ImVec2(x, fy), U(C(1, 1, 1), a), fps);
    dl->AddRectFilled(ImVec2(mx.x - side * 0.12f, mn.y + side * 0.12f), ImVec2(mx.x - side * 0.03f, mx.y - side * 0.12f), U(C(0.90f, 0.10f, 0.10f), a));   // red end cap
    return ic;
}

} // namespace fsui
