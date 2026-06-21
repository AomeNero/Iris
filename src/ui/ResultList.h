// Iris UI —— 结果列表（QPainter 自绘辅助类）
// 设计依据: doc/detailed-design.md §8.5, Alfred 5 高保真主题规格
#pragma once

#include <QString>
#include <QVector>

#include "provider/IProvider.h"

class QPainter;
class QRect;
class QPoint;

namespace iris {

class ResultListView {
public:
    void SetResults(const QVector<ResultItem>& results);
    void Clear() { results_.clear(); selectedIndex_ = 0; scrollOffset_ = 0; }

    int  GetCount() const { return results_.size(); }
    int  GetSelectedIndex() const { return selectedIndex_; }
    bool HasSelection() const { return selectedIndex_ >= 0 && selectedIndex_ < results_.size(); }
    const ResultItem* GetSelected() const;
    const ResultItem* GetVisibleItem(int visibleNo) const;  // 第N可见行(1..9)，与右侧 ctrlN 提示一一对应

    void MoveSelectionUp();
    void MoveSelectionDown();
    void ScrollBy(int delta);              // 鼠标滚轮：移动选择并保持可见
    void SelectByY(int yInList);           // 鼠标点击命中（相对列表顶部）
    bool SetHoverByY(int yInList);         // 鼠标悬停命中 → 直接选中该行；返回选中是否变化

    void SetEmptyHint(const QString& hint) { emptyHint_ = hint; }

    // 绘制到给定区域（紧贴输入栏下方）
    void Paint(QPainter& p, const QRect& listRect);

    // 计算列表区域总高度（空态 kEmptyHeight；否则上限 kMaxVisibleRows 行）
    int  CalculateHeight() const;

    static constexpr int kRowHSelected   = 92;
    static constexpr int kRowHNormal     = 92;   // 等高（Alfred 5 规格）
    static constexpr int kMaxVisibleRows = 9;    // Ctrl+1..Ctrl+9 映射到可见行
    static constexpr int kEmptyHeight    = 80;

private:
    void PaintRow(QPainter& p, int rowIdx, const QRect& rowRect);
    void DrawGlobe(QPainter& p, const QRect& iconRect, const QColor& color);   // BOOKMARK 矢量地球仪
    void DrawEnterAction(QPainter& p, const QRect& rowRect);                   // 选中行右侧 enter.png
    void DrawShortcut(QPainter& p, const QRect& rowRect, int visibleNo);       // 常态行右侧 ctrl.png+N
    void DrawScrollBar(QPainter& p, const QRect& listRect) const;

    int  VisibleCount() const { const int s = results_.size(); return s < kMaxVisibleRows ? s : kMaxVisibleRows; }
    int  VisibleRowHeight(int idx) const { return (idx == selectedIndex_) ? kRowHSelected : kRowHNormal; }
    void EnsureBounds();
    void EnsureSelectionVisible();

    QVector<ResultItem> results_;
    int selectedIndex_ = 0;
    int scrollOffset_  = 0;   // 第一个可见行的逻辑索引
    QString emptyHint_{QString::fromUtf8("开始输入以搜索")};
};

} // namespace iris
