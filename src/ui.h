#pragma once
#include "imgui.h"

class Engine;
class Settings;

struct UiContext {
    Settings* settings = nullptr;
    Engine* engine = nullptr;
    float scale = 1.0f;  // DPI scale
    ImFont* regular = nullptr;
    ImFont* semibold = nullptr;
    bool startupEnabled = false;
};

struct UiActions {
    bool hide = false;
    bool quit = false;
    bool toggleStartup = false;
};

namespace ui {
constexpr unsigned kBackground = 0x0f1115;  // window / title bar colour (0xRRGGBB)
void SetupStyle(float scale);
void Draw(UiContext& ctx, UiActions& out);
}  // namespace ui
