#include "ui.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "engine.h"
#include "imgui_internal.h"
#include "settings.h"
#include "util.h"

namespace {

// ---------------------------------------------------------------- theme
namespace col {
constexpr unsigned BG = ui::kBackground, SURFACE = 0x171a21, SURFACE2 = 0x20242d, BORDER = 0x262b35,
                   HOVER = 0x2b313c, GRID = 0x222731, TRACK = 0x2e3440, KNOB = 0x2d333f, KNOB_RING = 0x4a5262,
                   TEXT = 0xe9ecf2, TEXT2 = 0xa2aabb, TEXT3 = 0x6c7486, ACCENT = 0x4c8dff, ACCENT_HI = 0x74a8ff,
                   GOOD = 0x3ddc97, WARN = 0xffb547, BAD = 0xff6b6b, TARGET = 0xc9cfdb;
}

constexpr ImU32 U32(unsigned rgb, int a = 255) {
    return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, a);
}
ImVec4 V4(unsigned rgb, float a = 1.0f) {
    return ImVec4(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, a);
}
ImU32 Lerp(unsigned a, unsigned b, float t) {
    ImVec4 x = V4(a), y = V4(b);
    return ImGui::GetColorU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, 1.0f));
}

UiContext* g = nullptr;
float S(float v) { return v * g->scale; }
ImVec2 S(float x, float y) { return ImVec2(x * g->scale, y * g->scale); }
ImVec2 operator+(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }

// font sizes are unscaled; DPI scale is applied here
ImVec2 Measure(ImFont* f, float size, const char* s) { return f->CalcTextSizeA(S(size), FLT_MAX, 0.0f, s); }
void Text(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 c, const char* s) { dl->AddText(f, S(size), p, c, s); }

struct Preset {
    const char* name;
    double attack, release;
    const char* hint;
};
const Preset kPresets[] = {
    {"Gentle", 0.6, 6.0, "Subtle, slow changes. Keeps the movie's dynamics mostly intact."},
    {"Balanced", 0.25, 4.0, "Good for most shows and movies."},
    {"Night", 0.1, 2.0, "Strongest leveling. Keeps action close to dialogue level."},
};

// ---------------------------------------------------------------- widgets
void HandCursorIfHovered() {
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

// Animated on/off switch.
bool Toggle(const char* id, bool* v, ImVec2 size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, size);
    if (clicked) *v = !*v;
    HandCursorIfHovered();
    float& t = *ImGui::GetStateStorage()->GetFloatRef(ImGui::GetItemID(), *v ? 1.0f : 0.0f);
    t += ((*v ? 1.0f : 0.0f) - t) * std::min(1.0f, ImGui::GetIO().DeltaTime * 14.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = size.y * 0.5f;
    ImVec2 q = p + size;
    dl->AddRectFilled(p, q, U32(col::ACCENT, (int)(255 * t)), r);
    if (t < 0.99f) dl->AddRect(p + S(0.75f, 0.75f), q + S(-0.75f, -0.75f), U32(col::TEXT2, (int)(255 * (1 - t))), r, S(1.5f));
    const float kx = p.x + r + (size.x - 2 * r) * t;
    dl->AddCircleFilled(ImVec2(kx, p.y + r), r * (0.48f + 0.14f * t), Lerp(col::TEXT2, 0xffffff, t));
    return clicked;
}

// Label + value on one line, custom slider, hint below.
bool SliderRow(const char* label, double* v, double lo, double hi, double step, const char* fmt, const char* hint) {
    ImGui::PushID(label);
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();

    char val[32];
    snprintf(val, sizeof(val), fmt, *v);
    Text(dl, g->regular, 15, p, U32(col::TEXT), label);
    ImVec2 vs = Measure(g->semibold, 15, val);
    Text(dl, g->semibold, 15, ImVec2(p.x + w - vs.x, p.y), U32(col::ACCENT_HI), val);
    ImGui::Dummy(ImVec2(w, S(19)));

    const float h = S(24), r = S(9);
    p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##slider", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const float x0 = p.x + r, x1 = p.x + w - r, cy = p.y + h * 0.5f;
    auto snap = [&](double x) {
        x = Clamp(x, lo, hi);
        x = lo + std::round((x - lo) / step) * step;
        return Clamp(std::round(x * 10000.0) / 10000.0, lo, hi);
    };
    bool changed = false;
    if (active) {
        double t = Clamp((double)((ImGui::GetIO().MousePos.x - x0) / (x1 - x0)), 0.0, 1.0);
        double nv = snap(lo + t * (hi - lo));
        if (nv != *v) *v = nv, changed = true;
    } else if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        double nv = snap(*v + (ImGui::GetIO().MouseWheel > 0 ? step : -step));
        if (nv != *v) *v = nv, changed = true;
    }
    const float kx = x0 + (float)((Clamp(*v, lo, hi) - lo) / (hi - lo)) * (x1 - x0);
    const float th = S(2.0f);
    dl->AddRectFilled(ImVec2(x0, cy - th), ImVec2(x1, cy + th), U32(col::TRACK), th);
    dl->AddRectFilled(ImVec2(x0, cy - th), ImVec2(kx, cy + th), U32(col::ACCENT), th);
    dl->AddCircleFilled(ImVec2(kx, cy), r, U32(col::KNOB));
    dl->AddCircle(ImVec2(kx, cy), r - S(0.5f), U32(col::KNOB_RING), 0, S(1.0f));
    const bool hot = hovered || active;
    dl->AddCircleFilled(ImVec2(kx, cy), r * (hot ? 0.62f : 0.5f), U32(hot ? col::ACCENT_HI : col::ACCENT));

    ImGui::PushFont(g->regular, 13.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, V4(col::TEXT3));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
    ImGui::TextUnformatted(hint);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(1)));
    ImGui::PopID();
    return changed;
}

// Pill-shaped segmented control; returns the clicked index or -1.
int Segmented(const char* id, int selected) {
    ImGui::PushID(id);
    const int n = (int)std::size(kPresets);
    const float w = ImGui::GetContentRegionAvail().x, h = S(36), pad = S(3);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + ImVec2(w, h), U32(col::SURFACE2), S(8));
    const float segW = (w - 2 * pad) / n;
    int clicked = -1;
    for (int i = 0; i < n; ++i) {
        ImVec2 a(p.x + pad + segW * i, p.y + pad), b(a.x + segW, p.y + h - pad);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##seg", ImVec2(segW, b.y - a.y))) clicked = i;
        const bool hov = ImGui::IsItemHovered();
        HandCursorIfHovered();
        ImGui::PopID();
        if (i == selected) dl->AddRectFilled(a, b, U32(col::ACCENT), S(6));
        else if (hov) dl->AddRectFilled(a, b, U32(col::HOVER), S(6));
        ImVec2 ts = Measure(g->regular, 15, kPresets[i].name);
        Text(dl, g->regular, 15, ImVec2(a.x + (segW - ts.x) * 0.5f, a.y + (b.y - a.y - ts.y) * 0.5f),
             U32(i == selected ? 0xffffff : (hov ? col::TEXT : col::TEXT2)), kPresets[i].name);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(w, S(2)));
    ImGui::PopID();
    return clicked;
}

bool FlatButton(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Button, V4(col::SURFACE2));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, V4(col::HOVER));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, V4(0x353c49));
    bool r = ImGui::Button(label);
    HandCursorIfHovered();
    ImGui::PopStyleColor(3);
    return r;
}

float ButtonWidth(const char* label) {
    return Measure(g->regular, 15, label).x + ImGui::GetStyle().FramePadding.x * 2;
}

// Rounded card with an optional title row. size.y == 0 -> auto height.
void BeginCard(const char* id, ImVec2 size, const char* title = nullptr, const char* subtitle = nullptr) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V4(col::SURFACE));
    ImGui::PushStyleColor(ImGuiCol_Border, V4(col::BORDER));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, S(16, 14));
    ImGuiChildFlags flags = ImGuiChildFlags_Borders | (size.y == 0 ? ImGuiChildFlags_AutoResizeY : 0);
    ImGui::BeginChild(id, size, flags, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    if (title) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        Text(dl, g->semibold, 16, p, U32(col::TEXT), title);
        if (subtitle) {
            ImVec2 ss = Measure(g->regular, 13, subtitle);
            Text(dl, g->regular, 13, ImVec2(p.x + w - ss.x, p.y + S(2)), U32(col::TEXT3), subtitle);
        }
        ImGui::Dummy(ImVec2(w, S(27)));
    }
}
void EndCard() { ImGui::EndChild(); }

void StatCard(const char* id, ImVec2 size, const char* title, unsigned dot, const char* value, unsigned valueCol,
              const char* unit, const char* sub, float barPct) {
    BeginCard(id, size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    dl->AddCircleFilled(ImVec2(p.x + S(3.5f), p.y + S(9)), S(3.5f), U32(dot));
    Text(dl, g->regular, 13, ImVec2(p.x + S(13), p.y), U32(col::TEXT2), title);
    ImVec2 vs = Measure(g->semibold, 30, value);
    const float vy = p.y + S(18);
    Text(dl, g->semibold, 30, ImVec2(p.x, vy), U32(valueCol), value);
    ImVec2 us = Measure(g->regular, 15, unit);
    Text(dl, g->regular, 15, ImVec2(p.x + vs.x + S(5), vy + vs.y - us.y - S(5)), U32(col::TEXT2), unit);
    if (barPct >= 0) {
        float y = p.y + S(66), th = S(5);
        dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + w, y + th), U32(col::TRACK), th * 0.5f);
        dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + w * Clamp(barPct, 0.0f, 100.0f) / 100.0f, y + th), U32(col::WARN), th * 0.5f);
    } else {
        Text(dl, g->regular, 13, ImVec2(p.x, p.y + S(62)), U32(col::TEXT3), sub);
    }
    EndCard();
}

void DrawLogo(ImDrawList* dl, ImVec2 p, float sz, bool active) {
    const int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(p, p + ImVec2(sz, sz), IM_COL32_WHITE, sz * 0.24f);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, p, ImVec2(p.x, p.y + sz),
                                                  U32(active ? 0x5b9bff : 0x767c8a), U32(active ? 0x7b61ff : 0x545a68));
    const float k = sz / 64.0f;
    auto P = [&](float x, float y) { return ImVec2(p.x + x * k, p.y + y * k); };
    const ImU32 w = IM_COL32_WHITE;
    dl->AddRectFilled(P(11, 26), P(18.5f, 38), w);
    dl->AddQuadFilled(P(18, 26), P(28, 17), P(28, 47), P(18, 38), w);
    const float hs[3] = {14, 24, 14};
    for (int i = 0; i < 3; ++i) {
        float x = 34 + i * 7.5f;
        dl->AddRectFilled(P(x, 32 - hs[i] / 2), P(x + 4.5f, 32 + hs[i] / 2), w, 2.25f * k);
    }
}

// ---------------------------------------------------------------- graph
void DrawGraph(ImVec2 p, ImVec2 size, const std::vector<HistPoint>& hist, double now) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lp = S(30), vp = S(8);
    const ImVec2 a(p.x + lp, p.y), b(p.x + size.x, p.y + size.y);
    const double span = Engine::kHistSeconds;
    auto Y = [&](double db) { return a.y + vp + (float)((0.0 - Clamp(db, -70.0, 0.0)) / 70.0) * (size.y - 2 * vp); };
    auto X = [&](double t) { return b.x - (float)((now - t) / span) * (b.x - a.x); };

    for (int db = -70; db <= 0; db += 10) {
        float y = Y(db);
        dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), U32(col::GRID), S(1));
        char lbl[8];
        snprintf(lbl, sizeof(lbl), "%d", db);
        ImVec2 ts = Measure(g->regular, 12, lbl);
        Text(dl, g->regular, 12, ImVec2(a.x - S(8) - ts.x, y - ts.y * 0.5f), U32(col::TEXT3), lbl);
    }
    for (int sec : {10, 20}) {
        float x = X(now - sec);
        dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), U32(col::GRID), S(1));
    }

    auto segments = [&](auto getter) {
        std::vector<std::vector<ImVec2>> out(1);
        for (const auto& h : hist) {
            double v = getter(h);
            if (std::isnan(v)) {
                if (out.back().size() >= 2) out.emplace_back();
                else out.back().clear();
            } else {
                out.back().push_back(ImVec2(X(h.t), Y(v)));
            }
        }
        if (out.back().size() < 2) out.pop_back();
        return out;
    };
    const float lw = S(2.0f);
    auto line = [&](const std::vector<std::vector<ImVec2>>& segs, unsigned c) {
        for (const auto& s : segs) dl->AddPolyline(s.data(), (int)s.size(), U32(c), lw);
        if (!segs.empty() && segs.back().back().x > b.x - S(4)) dl->AddCircleFilled(segs.back().back(), S(3.5f), U32(c));
    };

    dl->PushClipRect(a, b, true);
    // target (dashed)
    for (const auto& s : segments([](const HistPoint& h) { return (double)h.target; })) {
        float dist = 0, period = S(10);
        for (size_t i = 1; i < s.size(); ++i) {
            if ((int)(dist / period) % 2 == 0) dl->AddLine(s[i - 1], s[i], U32(col::TARGET, 150), S(1));
            dist += std::hypot(s[i].x - s[i - 1].x, s[i].y - s[i - 1].y);
        }
    }
    line(segments([](const HistPoint& h) { return (double)h.volDb; }), col::WARN);
    auto content = segments([](const HistPoint& h) { return (double)h.content; });
    const ImDrawListFlags saved = dl->Flags;
    dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;  // avoid seams between fill quads
    for (const auto& s : content)
        for (size_t i = 1; i < s.size(); ++i)
            dl->AddQuadFilled(s[i - 1], s[i], ImVec2(s[i].x, b.y), ImVec2(s[i - 1].x, b.y), U32(col::ACCENT, 34));
    dl->Flags = saved;
    line(content, col::ACCENT);
    line(segments([](const HistPoint& h) { return (double)h.content + h.volDb; }), col::GOOD);
    dl->PopClipRect();
}

std::vector<HistPoint> g_hist;

}  // namespace

// ==================================================================== public
void ui::SetupStyle(float scale) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowPadding = ImVec2(22, 18);
    st.FramePadding = ImVec2(16, 8);
    st.ItemSpacing = ImVec2(8, 6);
    st.WindowRounding = 0;
    st.ChildRounding = 10;
    st.FrameRounding = 6;
    st.WindowBorderSize = 0;
    st.ChildBorderSize = 1;
    st.ScaleAllSizes(scale);
    st.FontScaleDpi = scale;
    st.FontSizeBase = 15.0f;

    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = V4(col::BG);
    c[ImGuiCol_ChildBg] = V4(col::SURFACE);
    c[ImGuiCol_Border] = V4(col::BORDER);
    c[ImGuiCol_Text] = V4(col::TEXT);
    c[ImGuiCol_TextDisabled] = V4(col::TEXT3);
    c[ImGuiCol_Button] = V4(col::SURFACE2);
    c[ImGuiCol_ButtonHovered] = V4(col::HOVER);
    c[ImGuiCol_ButtonActive] = V4(0x353c49);
    c[ImGuiCol_NavCursor] = V4(col::ACCENT);
}

void ui::Draw(UiContext& ctx, UiActions& out) {
    g = &ctx;
    Settings& settings = *ctx.settings;
    const EngineStatus st = ctx.engine->Status();
    const Core c = settings.Get();
    ctx.engine->History(g_hist);
    const double now = NowSeconds();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushFont(ctx.regular, 15.0f);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ---------------------------------------------------------------- header
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const float fullW = ImGui::GetContentRegionAvail().x;
    DrawLogo(dl, o, S(42), c.enabled);
    Text(dl, ctx.semibold, 24, ImVec2(o.x + S(56), o.y - S(2)), U32(col::TEXT), "AutoVolume");
    std::string subtitle = "Automatic loudness leveling";
    if (!st.device.empty()) subtitle += "  \xC2\xB7  " + st.device;
    Text(dl, ctx.regular, 13, ImVec2(o.x + S(56), o.y + S(27)), U32(col::TEXT2), subtitle.c_str());

    const ImVec2 swSize = S(46, 24);
    ImGui::SetCursorScreenPos(ImVec2(o.x + fullW - swSize.x, o.y + S(9)));
    bool enabled = c.enabled;
    if (Toggle("##enabled", &enabled, swSize)) settings.Update([&](Core& x) { x.enabled = enabled; });
    const char* onOff = c.enabled ? "On" : "Off";
    ImVec2 oos = Measure(ctx.semibold, 15, onOff);
    const float onX = o.x + fullW - swSize.x - S(10) - oos.x;
    Text(dl, ctx.semibold, 15, ImVec2(onX, o.y + S(21) - oos.y * 0.5f), U32(c.enabled ? col::TEXT : col::TEXT2), onOff);

    const char* pill;
    unsigned pillCol;
    if (st.error) pill = "Error", pillCol = col::BAD;
    else if (st.state != "Running") pill = st.state.c_str(), pillCol = col::TEXT3;
    else if (!c.enabled) pill = "Paused", pillCol = col::WARN;
    else if (st.muted) pill = "Muted", pillCol = col::TEXT3;
    else if (!st.hasContent) pill = "Waiting for sound", pillCol = col::TEXT3;
    else pill = "Leveling", pillCol = col::GOOD;
    ImVec2 ps = Measure(ctx.regular, 13, pill);
    const float pillW = ps.x + S(12 + 8 + 7 + 12), pillH = S(30);
    const ImVec2 pa(onX - S(18) - pillW, o.y + S(21) - pillH * 0.5f);
    dl->AddRectFilled(pa, pa + ImVec2(pillW, pillH), U32(col::SURFACE2), pillH * 0.5f);
    dl->AddCircleFilled(ImVec2(pa.x + S(16), pa.y + pillH * 0.5f), S(4), U32(pillCol));
    Text(dl, ctx.regular, 13, ImVec2(pa.x + S(27), pa.y + (pillH - ps.y) * 0.5f), U32(col::TEXT2), pill);

    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + S(42 + 20)));

    // ---------------------------------------------------------------- body
    const float bodyH = ImGui::GetContentRegionAvail().y;
    const float rightW = S(340), colGap = S(16), gap = S(14);
    const float leftW = fullW - rightW - colGap;
    const ImGuiWindowFlags plain = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::BeginChild("##left", ImVec2(leftW, bodyH), ImGuiChildFlags_None, plain);
    {
        const float statH = S(112), sgap = S(12), sw = (leftW - 2 * sgap) / 3;
        char v[32];
        if (st.hasContent) snprintf(v, sizeof(v), "%.0f", st.content);
        StatCard("##s0", ImVec2(sw, statH), "Program loudness", col::ACCENT, st.hasContent ? v : "--", col::TEXT,
                 st.hasContent ? "LUFS" : "", st.hasContent ? "what's playing now" : "silence", -1);
        ImGui::SameLine(0, sgap);
        if (c.enabled && st.hasVol && st.hasBase) {
            const double adj = st.volDb - st.baseDb;
            snprintf(v, sizeof(v), "%+.1f", adj);
            const bool up = adj > 0.5, down = adj < -0.5;
            StatCard("##s1", ImVec2(sw, statH), "Adjustment", col::GOOD, v, up ? col::GOOD : down ? col::WARN : col::TEXT,
                     "dB", up ? "boosting quiet audio" : down ? "taming loud audio" : "no change needed", -1);
        } else {
            StatCard("##s1", ImVec2(sw, statH), "Adjustment", col::GOOD, "--", col::TEXT, "",
                     c.enabled ? "starting" : "paused", -1);
        }
        ImGui::SameLine(0, sgap);
        snprintf(v, sizeof(v), "%.0f", st.volPct);
        StatCard("##s2", ImVec2(sw, statH), "Windows volume", col::WARN, st.hasVol ? v : "--", col::TEXT,
                 st.hasVol ? "%" : "", "", st.hasVol ? (float)st.volPct : 0.0f);

        const float infoH = S(62), footH = S(44);
        const float graphH = bodyH - statH - infoH - footH - 3 * gap;
        ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
        BeginCard("##graph", ImVec2(leftW, graphH), "Live loudness", "Last 30 seconds");
        {
            ImVec2 gp = ImGui::GetCursorScreenPos();
            ImVec2 avail = ImGui::GetContentRegionAvail();
            const float legendH = S(26);
            DrawGraph(gp, ImVec2(avail.x, avail.y - legendH), g_hist, now);
            ImVec2 lp(gp.x, gp.y + avail.y - S(16));
            struct L { const char* t; unsigned c; } legend[] = {
                {"What's playing", col::ACCENT}, {"What you hear", col::GOOD}, {"Windows volume", col::WARN}, {"Target", col::TARGET}};
            for (const auto& l : legend) {
                dl = ImGui::GetWindowDrawList();
                dl->AddCircleFilled(ImVec2(lp.x + S(4), lp.y + S(8)), S(4), U32(l.c));
                Text(dl, ctx.regular, 13, ImVec2(lp.x + S(13), lp.y), U32(col::TEXT2), l.t);
                lp.x += S(13) + Measure(ctx.regular, 13, l.t).x + S(20);
            }
        }
        EndCard();

        // info bar
        ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, V4(col::SURFACE2));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, S(16, 12));
        ImGui::BeginChild("##info", ImVec2(leftW, infoH), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        {
            std::string msg;
            unsigned iconCol = col::ACCENT_HI;
            bool warn = false;
            if (st.error) {
                msg = "Audio problem: " + st.state.substr(st.state.rfind("Error: ", 0) == 0 ? 7 : 0);
                iconCol = col::BAD, warn = true;
            } else if (c.enabled && st.hasBase && st.baseDb > -0.75) {
                msg = "Windows volume is at 100%, so AutoVolume can only turn things down. Set Windows to about 50% "
                      "and turn your TV or speakers up instead, so quiet dialogue can be boosted.";
                iconCol = col::WARN, warn = true;
            } else {
                msg = "Use your volume keys as usual. AutoVolume learns the level you like and returns your volume "
                      "to normal when you switch it off or quit.";
            }
            dl = ImGui::GetWindowDrawList();
            ImVec2 ip = ImGui::GetCursorScreenPos();
            const float textH = infoH - S(24);
            ImVec2 ic(ip.x + S(9), ip.y + textH * 0.5f);
            if (warn) {
                dl->AddTriangleFilled(ImVec2(ic.x, ic.y - S(8)), ImVec2(ic.x + S(9), ic.y + S(7)), ImVec2(ic.x - S(9), ic.y + S(7)), U32(iconCol));
                Text(dl, ctx.semibold, 12, ImVec2(ic.x - S(2), ic.y - S(5)), U32(col::SURFACE2), "!");
            } else {
                dl->AddCircle(ic, S(8), U32(iconCol), 0, S(1.5f));
                Text(dl, ctx.semibold, 12, ImVec2(ic.x - S(1.8f), ic.y - S(8)), U32(iconCol), "i");
            }
            ImGui::PushFont(ctx.regular, 13.0f);
            const float tx = ImGui::GetCursorPosX() + S(30);
            const float wrapW = ImGui::GetContentRegionAvail().x - S(30);
            ImVec2 ts = ImGui::CalcTextSize(msg.c_str(), nullptr, false, wrapW);
            ImGui::SetCursorPos(ImVec2(tx, ImGui::GetCursorPosY() + std::max(0.0f, (textH - ts.y) * 0.5f)));
            ImGui::PushStyleColor(ImGuiCol_Text, V4(col::TEXT2));
            ImGui::PushTextWrapPos(tx + wrapW);
            ImGui::TextUnformatted(msg.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        ImGui::EndChild();

        // footer
        const float fy = ImGui::GetWindowPos().y + bodyH - footH;
        const float fx = ImGui::GetWindowPos().x;
        ImGui::SetCursorScreenPos(ImVec2(fx, fy + (footH - S(22)) * 0.5f));
        bool startup = ctx.startupEnabled;
        if (Toggle("##startup", &startup, S(40, 22))) out.toggleStartup = true;
        dl = ImGui::GetWindowDrawList();
        ImVec2 sts = Measure(ctx.regular, 15, "Start with Windows");
        Text(dl, ctx.regular, 15, ImVec2(fx + S(52), fy + (footH - sts.y) * 0.5f), U32(col::TEXT2), "Start with Windows");

        const float bh = ImGui::GetFrameHeight();
        const float qW = ButtonWidth("Quit"), hW = ButtonWidth("Hide to tray");
        ImGui::SetCursorScreenPos(ImVec2(fx + leftW - qW - hW - S(8), fy + (footH - bh) * 0.5f));
        if (FlatButton("Hide to tray")) out.hide = true;
        ImGui::SameLine(0, S(8));
        if (FlatButton("Quit")) out.quit = true;
    }
    ImGui::EndChild();

    ImGui::SameLine(0, colGap);
    ImGui::BeginChild("##right", ImVec2(rightW, bodyH), ImGuiChildFlags_None, plain);
    {
        int sel = -1;
        for (int i = 0; i < (int)std::size(kPresets); ++i)
            if (std::fabs(c.attack - kPresets[i].attack) < 1e-6 && std::fabs(c.release - kPresets[i].release) < 1e-6) sel = i;
        BeginCard("##mode", ImVec2(rightW, 0), "Leveling mode");
        int clicked = Segmented("##preset", sel);
        if (clicked >= 0)
            settings.Update([&](Core& x) {
                x.attack = kPresets[clicked].attack;
                x.release = kPresets[clicked].release;
            });
        ImGui::Dummy(ImVec2(0, S(2)));
        ImGui::PushFont(ctx.regular, 13.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, V4(col::TEXT3));
        ImGui::TextWrapped("%s", sel >= 0 ? kPresets[sel].hint : "Custom - set with the sliders below.");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        EndCard();

        ImGui::Dummy(ImVec2(0, S(12) - ImGui::GetStyle().ItemSpacing.y));
        BeginCard("##leveling", ImVec2(rightW, 0), "Leveling");
        double t = c.target, at = c.attack, rel = c.release;
        if (SliderRow("Listening level", &t, -75, -10, 0.5, "%.1f dB", "Overall loudness. Your volume keys adjust it too."))
            settings.Update([&](Core& x) { x.target = t; x.hasTarget = true; });
        if (SliderRow("Turn-down speed", &at, 0.05, 2.0, 0.05, "%.2f s", "Lower = faster on gunshots and loud music."))
            settings.Update([&](Core& x) { x.attack = at; });
        if (SliderRow("Turn-up speed", &rel, 0.5, 15.0, 0.5, "%.1f s", "Lower = quiet dialogue comes back sooner."))
            settings.Update([&](Core& x) { x.release = rel; });
        EndCard();

        ImGui::Dummy(ImVec2(0, S(12) - ImGui::GetStyle().ItemSpacing.y));
        BeginCard("##limits", ImVec2(rightW, 0), "Limits");
        double mx = c.maxPct < 0 ? 100 : c.maxPct, mn = c.minPct < 0 ? 0 : c.minPct, gate = c.gate;
        if (SliderRow("Max volume", &mx, 1, 100, 1, "%.0f%%", "Ceiling, so quiet background hiss isn't blasted."))
            settings.Update([&](Core& x) {
                x.maxPct = (int)mx;
                if (x.minPct > x.maxPct) x.minPct = x.maxPct;
            });
        if (SliderRow("Min volume", &mn, 0, 100, 1, "%.0f%%", "Floor for the Windows volume."))
            settings.Update([&](Core& x) {
                x.minPct = (int)mn;
                if (x.maxPct < x.minPct) x.maxPct = x.minPct;
            });
        if (SliderRow("Silence threshold", &gate, -70, -30, 1, "%.0f LUFS", "Anything quieter is treated as silence."))
            settings.Update([&](Core& x) { x.gate = gate; });
        EndCard();
    }
    ImGui::EndChild();

    ImGui::End();
    ImGui::PopFont();
}
