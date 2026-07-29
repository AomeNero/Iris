// Iris Core —— DPI 自动适配实现
#include "core/DpiHelper.h"

#include <QApplication>
#include <QDesktopWidget>
#include <QScreen>

namespace iris {

float DpiHelper::scaleFactor_ = 1.0f;

void DpiHelper::Init(QApplication& app) {
    // 获取主屏幕 DPI
    QDesktopWidget* desktop = app.desktop();
    if (!desktop) return;

    const int dpi = desktop->logicalDpiX();  // 水平 DPI（96=100%, 120=125%, 144=150%）
    const int physicalWidth = desktop->screenGeometry().width();  // 物理像素宽度

    // 逻辑像素宽度 = 物理宽度 / DPI缩放因子
    const float dpiScale = static_cast<float>(dpi) / 96.0f;
    const float logicalWidth = static_cast<float>(physicalWidth) / dpiScale;

    // 基准：150% DPI (144) + 1920 逻辑宽
    constexpr float kBaseDpi = 144.0f;
    constexpr float kBaseWidth = 1920.0f;

    const float dpiFactor = static_cast<float>(dpi) / kBaseDpi;
    const float widthFactor = logicalWidth / kBaseWidth;

    scaleFactor_ = dpiFactor * widthFactor;

    // 钳制到合理范围 [0.5, 3.0]
    if (scaleFactor_ < 0.5f) scaleFactor_ = 0.5f;
    if (scaleFactor_ > 3.0f) scaleFactor_ = 3.0f;
}

int DpiHelper::Scale(int basePx) {
    return static_cast<int>(basePx * scaleFactor_ + 0.5f);
}

int DpiHelper::ScaleFont(int basePt) {
    return static_cast<int>(basePt * scaleFactor_ + 0.5f);
}

} // namespace iris
