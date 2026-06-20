// Iris UI —— 主题调色板实现
#include "ui/Theme.h"

#include <cctype>

namespace iris {

namespace {

Palette MakeLight() {
    Palette p;
    p.base            = QColor("#e9e9e9");
    p.inputBg         = QColor("#d2d2d2");
    p.hoverRow        = QColor("#E7E7EC");
    p.accent          = QColor("#5a2178");
    p.hotkey          = QColor("#5a2178");   // Ctrl+数字
    p.accentSoft      = QColor("#D1C4E9");
    p.textPrimary     = QColor("#212121");
    p.textSecondary   = QColor("#757575");
    p.inputText       = Qt::black;
    p.selectedFg      = Qt::white;
    p.border          = QColor(0, 0, 0, 20);
    p.iconFallback    = QColor(200, 200, 200);
    p.iconFallbackSel = QColor(255, 255, 255, 40);
    p.scrollbar       = QColor(120, 120, 130, 96);
    p.irisFallback    = QColor(180, 180, 180);
    return p;
}

Palette MakeDark() {
    Palette p;
    p.base            = QColor("#3e3e3e");
    p.inputBg         = QColor("#2f2f2f");
    p.hoverRow        = QColor("#2A2A30");
    p.accent          = QColor("#328188");   // 品牌紫不变
    p.hotkey          = QColor("#9d9d9d");   // Ctrl+数字
    p.accentSoft      = QColor("#D1C4E9");   // 不变
    p.textPrimary     = QColor("#ECECEC");
    p.textSecondary   = QColor("#9A9AA0");
    p.inputText       = QColor("#ECECEC");
    p.selectedFg      = Qt::white;           // 不变
    p.border          = QColor(255, 255, 255, 25);
    p.iconFallback    = QColor(0x3A, 0x3A, 0x40);
    p.iconFallbackSel = QColor(255, 255, 255, 40);   // 不变
    p.scrollbar       = QColor(160, 160, 170, 110);
    p.irisFallback    = QColor(0x5A, 0x5A, 0x60);
    return p;
}

} // namespace

Palette MakePalette(Theme t) {
    return (t == Theme::Dark) ? MakeDark() : MakeLight();
}

namespace {
Palette g_current = MakePalette(Theme::Light);  // 默认 light；main 启动时覆盖
} // namespace

const Palette& CurrentPalette() { return g_current; }

void SetCurrentTheme(Theme t) { g_current = MakePalette(t); }

Theme ParseThemeName(const std::string& s) {
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return (lower == "dark") ? Theme::Dark : Theme::Light;  // "light" 及未知值 → Light
}

} // namespace iris
