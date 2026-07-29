// Iris Core —— DPI 自动适配
// 基准环境：150% DPI (144) + 1920 逻辑宽
// 缩放公式：scaleFactor = (DPI/150) × (逻辑宽/1920)
#pragma once

class QApplication;

namespace iris {

class DpiHelper {
public:
    /// 启动时调用一次，检测主显示器 DPI 和逻辑分辨率，计算缩放因子
    static void Init(QApplication& app);

    /// 缩放像素值（基准 = 150% DPI + 1920 逻辑宽下的像素值）
    static int Scale(int basePx);

    /// 缩放字体大小（point size）
    static int ScaleFont(int basePt);

    /// 获取当前缩放因子（调试用）
    static float ScaleFactor() { return scaleFactor_; }

private:
    static float scaleFactor_;
};

} // namespace iris
