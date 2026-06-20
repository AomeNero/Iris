// Iris UI —— 主题调色板（light / dark）
// 集中所有自绘颜色；画图代码经 CurrentPalette() 读取，启动时由 SetCurrentTheme 按配置设定。
#pragma once

#include <string>

#include <QColor>

namespace iris {

enum class Theme { Light, Dark };

// 14 个语义颜色。品牌紫（accent/accentSoft）跨主题保持一致，只换中性色。
struct Palette {
    QColor base;             // 窗体基底 + 常态行底
    QColor inputBg;          // 输入框底
    QColor accent;           // 品牌紫：选中行底
    QColor hotkey;           // Ctrl+数字 快捷字颜色
    QColor accentSoft;       // 选中行副标题文字
    QColor textPrimary;      // 常态标题文字
    QColor textSecondary;    // 常态副标题 / globe / 图标字母
    QColor inputText;        // 输入文字 + 光标
    QColor selectedFg;       // 选中行前景（文字/图标/地球仪）
    QColor border;           // 窗体细边框
    QColor iconFallback;     // 常态图标字母兜底底色
    QColor iconFallbackSel;  // 选中图标字母兜底底色
    QColor scrollbar;        // 滚动条滑块
    QColor irisFallback;     // iris 图标兜底
};

Palette MakePalette(Theme t);

/// 当前生效调色板（画图代码统一读取）。SetCurrentTheme 后立即生效。
const Palette& CurrentPalette();
/// 设定当前主题（启动时按 config.theme 调用一次）。
void SetCurrentTheme(Theme t);

/// "light"/"dark" → Theme；未知值 → Light。
Theme ParseThemeName(const std::string& s);

} // namespace iris
