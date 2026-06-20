// Iris UI —— ResultList 实现
#include "ui/ResultList.h"

#include <QPainter>
#include <QFontMetrics>
#include <QRect>
#include <QPixmap>
#include <QString>

#include <string>
#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <QtWin>

namespace iris {

namespace {

// ── 文件局部图标加载器 ──
// 用 SHGetFileInfoW 取系统图标索引，再经 SHGetImageList 取高清位图（JUMBO 256 →
// EXTRALARGE 48 → LARGE 32），QtWin::fromHICON 转 QPixmap，按路径缓存。
// 仅处理 APP/FILE；BOOKMARK 无 OS 图标，由 DrawGlobe 画矢量地球仪。
class IconLoader {
public:
    QPixmap Get(const std::wstring& path) {
        if (path.empty()) return {};
        auto it = cache_.find(path);
        if (it != cache_.end()) return it->second;
        QPixmap pm = LoadFromSystem(path);
        cache_.emplace(path, pm);
        return pm;
    }
private:
    static QPixmap LoadFromSystem(const std::wstring& path) {
        SHFILEINFOW fi{};
        if (!SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &fi, sizeof(fi),
                            SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES)) {
            // 兜底：直接取 LARGEICON（32px）
            return LoadPlain(path);
        }
        const int idx = fi.iIcon;
        for (int shil : {SHIL_JUMBO, SHIL_EXTRALARGE, SHIL_LARGE}) {
            IImageList* pImg = nullptr;
            if (SUCCEEDED(SHGetImageList(shil, IID_PPV_ARGS(&pImg))) && pImg) {
                HICON hIcon = nullptr;
                const HRESULT hr = pImg->GetIcon(idx, ILD_TRANSPARENT, &hIcon);
                pImg->Release();
                if (SUCCEEDED(hr) && hIcon) {
                    QPixmap pm = QtWin::fromHICON(hIcon);
                    DestroyIcon(hIcon);
                    if (!pm.isNull()) return pm;
                }
            }
        }
        return LoadPlain(path);
    }
    static QPixmap LoadPlain(const std::wstring& path) {
        SHFILEINFOW fi{};
        if (SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &fi, sizeof(fi),
                           SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES) && fi.hIcon) {
            QPixmap pm = QtWin::fromHICON(fi.hIcon);
            DestroyIcon(fi.hIcon);
            return pm;
        }
        return {};
    }
    std::unordered_map<std::wstring, QPixmap> cache_;
};

IconLoader& Icons() { static IconLoader inst; return inst; }

const QPixmap& EnterPixWhite() {
    // enter.png 染纯白（选中行紫底需白色图标）：用 alpha 通道 + 白色填充
    static QPixmap pm;
    if (pm.isNull()) {
        QPixmap src;
        if (src.load(":/enter.png") && !src.isNull()) {
            pm = QPixmap(src.size());
            pm.fill(Qt::transparent);
            QPainter gp(&pm);
            gp.drawPixmap(0, 0, src);
            gp.setCompositionMode(QPainter::CompositionMode_SourceIn);
            gp.fillRect(pm.rect(), Qt::white);
        }
    }
    return pm;
}
} // namespace

void ResultListView::SetResults(const QVector<ResultItem>& results) {
    results_ = results;
    if (selectedIndex_ >= results_.size()) selectedIndex_ = results_.size() - 1;
    if (selectedIndex_ < 0) selectedIndex_ = 0;
    hoveredIndex_ = -1;
    EnsureBounds();
    EnsureSelectionVisible();
}

const ResultItem* ResultListView::GetSelected() const {
    if (!HasSelection()) return nullptr;
    return &results_[selectedIndex_];
}

void ResultListView::EnsureBounds() {
    if (results_.isEmpty()) { selectedIndex_ = 0; scrollOffset_ = 0; return; }
    if (selectedIndex_ < 0) selectedIndex_ = 0;
    if (selectedIndex_ >= results_.size()) selectedIndex_ = results_.size() - 1;
    const int maxOff = results_.size() - VisibleCount();
    if (scrollOffset_ < 0) scrollOffset_ = 0;
    if (scrollOffset_ > maxOff) scrollOffset_ = maxOff;
}

void ResultListView::EnsureSelectionVisible() {
    if (results_.isEmpty()) { scrollOffset_ = 0; return; }
    const int visible = VisibleCount();
    if (results_.size() <= visible) { scrollOffset_ = 0; return; }
    if (scrollOffset_ > selectedIndex_) scrollOffset_ = selectedIndex_;
    if (scrollOffset_ < selectedIndex_ - visible + 1)
        scrollOffset_ = selectedIndex_ - visible + 1;
    EnsureBounds();
}

void ResultListView::MoveSelectionUp() {
    if (selectedIndex_ > 0) --selectedIndex_;
    EnsureSelectionVisible();
}
void ResultListView::MoveSelectionDown() {
    if (selectedIndex_ < results_.size() - 1) ++selectedIndex_;
    EnsureSelectionVisible();
}

void ResultListView::ScrollBy(int delta) {
    if (results_.isEmpty()) return;
    selectedIndex_ += delta;
    EnsureBounds();
    EnsureSelectionVisible();
}

void ResultListView::SelectByY(int y) {
    if (results_.isEmpty()) return;
    const int visible = VisibleCount();
    int acc = 0;
    for (int vi = 0; vi < visible; ++vi) {
        const int idx = scrollOffset_ + vi;
        const int rowH = VisibleRowHeight(idx);
        if (y < acc + rowH) { selectedIndex_ = idx; EnsureSelectionVisible(); return; }
        acc += rowH;
    }
    selectedIndex_ = scrollOffset_ + visible - 1;
    EnsureSelectionVisible();
}

bool ResultListView::SetHoverByY(int y) {
    if (results_.isEmpty()) {
        const bool changed = (hoveredIndex_ != -1);
        hoveredIndex_ = -1;
        return changed;
    }
    const int visible = VisibleCount();
    int acc = 0;
    int hit = scrollOffset_ + visible - 1;
    for (int vi = 0; vi < visible; ++vi) {
        const int idx = scrollOffset_ + vi;
        const int rowH = VisibleRowHeight(idx);
        if (y < acc + rowH) { hit = idx; break; }
        acc += rowH;
    }
    if (hit == hoveredIndex_) return false;
    hoveredIndex_ = hit;
    return true;
}

int ResultListView::CalculateHeight() const {
    if (results_.isEmpty()) return 0;  // 空列表不占位（窗口只保留输入框）
    return VisibleCount() * kRowHSelected;  // 等高 92
}

void ResultListView::DrawGlobe(QPainter& p, const QRect& r, const QColor& color) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color); pen.setWidthF(2.2); p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(r);
    // 经线（窄椭圆）
    p.drawEllipse(QRectF(r.center().x() - r.width() * 0.25, r.y(),
                         r.width() * 0.5, r.height()));
    // 赤道
    const int my = r.y() + r.height() / 2;
    p.drawLine(r.x() + 2, my, r.right() - 2, my);
    // 两条纬线
    p.drawLine(QPointF(r.x() + r.width() * 0.16, r.y() + r.height() * 0.28),
               QPointF(r.right() - r.width() * 0.16, r.y() + r.height() * 0.28));
    p.drawLine(QPointF(r.x() + r.width() * 0.16, r.y() + r.height() * 0.72),
               QPointF(r.right() - r.width() * 0.16, r.y() + r.height() * 0.72));
    p.restore();
}

void ResultListView::DrawEnterAction(QPainter& p, const QRect& r) {
    const QPixmap& pix = EnterPixWhite();
    if (pix.isNull()) return;
    const int sz = 30;  // 30x30，行内垂直居中，距右边框 70px
    const QRect er(r.right() - 70 - sz, r.top() + (r.height() - sz) / 2, sz, sz);
    p.drawPixmap(er, pix);
}

void ResultListView::DrawShortcut(QPainter& p, const QRect& r, int visibleNo) {
    if (visibleNo < 1 || visibleNo > 9) return;
    // 纯字符 "ctrlN"（如 ctrl1、ctrl2），距右边框 40px
    QFont f("Microsoft YaHei", 16);
    p.setFont(f);
    p.setPen(QColor("#5a2178"));
    const QString text = QString::fromUtf8("ctrl") + QString::number(visibleNo);
    const QFontMetrics fm(f);
    const int w = fm.horizontalAdvance(text);
    p.drawText(QRect(r.right() - 40 - w, r.top(), w, r.height()),
               Qt::AlignLeft | Qt::AlignVCenter, text);
}

void ResultListView::PaintRow(QPainter& p, int rowIdx, const QRect& r, bool hovered) {
    const ResultItem& item = results_[rowIdx];
    const bool selected = (rowIdx == selectedIndex_);
    const int padH = 18;
    const int iconSize = selected ? 64 : 56;

    // 行背景：左右各 18px(padH) 间距、12px 圆角的圆角矩形
    // 选中=皇家紫；常态=#e9e9e9（与窗体基底一致，常态不浮现色块）；悬停=极淡
    const QRect bgRect = r.adjusted(padH, 0, -padH, 0);
    p.setPen(Qt::NoPen);
    if (selected) {
        p.setBrush(QColor("#5a2178"));
    } else if (hovered) {
        p.setBrush(QColor("#E7E7EC"));
    } else {
        p.setBrush(QColor("#e9e9e9"));
    }
    p.drawRoundedRect(bgRect, 12, 12);

    // 图标距行左侧边框 36px（背景圆角矩形左缘在 padH=18，图标位于其内侧）
    const QRect iconRect(r.left() + 36, r.top() + (r.height() - iconSize) / 2,
                         iconSize, iconSize);
    const QString titleQ = QString::fromStdWString(item.title);
    if (item.type == ItemType::BOOKMARK) {
        DrawGlobe(p, iconRect, selected ? Qt::white : QColor("#757575"));
    } else {
        const QPixmap ico = Icons().Get(item.path);
        if (!ico.isNull()) {
            p.drawPixmap(iconRect, ico.scaled(iconSize, iconSize,
                                              Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(selected ? QColor(255, 255, 255, 40) : QColor(200, 200, 200));
            p.drawRoundedRect(iconRect, 10, 10);
            p.setPen(selected ? Qt::white : QColor("#757575"));
            QFont f("Microsoft YaHei", selected ? 22 : 18); f.setBold(true);
            p.setFont(f);
            p.drawText(iconRect, Qt::AlignCenter, titleQ.left(1).toUpper());
        }
    }

    // 双行文本：第一行 Title(微软雅黑 16 加粗)，第二行 路径(12)；块垂直居中
    const int textX = iconRect.right() + 16;
    const int textW = r.right() - padH - textX - 96;

    static const QFont kTitleFont = []() { QFont f("Microsoft YaHei", 16); f.setBold(true); return f; }();
    static const QFont kSubFont("Microsoft YaHei", 12);

    const QFontMetrics tm(kTitleFont);
    const QFontMetrics sm(kSubFont);
    const int blockH = tm.height() + sm.height();
    const int titleTop = r.top() + (r.height() - blockH) / 2;
    const int subTop = titleTop + tm.height();

    p.setFont(kTitleFont);
    p.setPen(selected ? Qt::white : QColor("#212121"));
    p.drawText(QRect(textX, titleTop, textW, tm.height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               tm.elidedText(titleQ, Qt::ElideRight, textW));

    p.setFont(kSubFont);
    p.setPen(selected ? QColor("#D1C4E9") : QColor("#757575"));
    const QString subQ = QString::fromStdWString(item.subtitle);
    p.drawText(QRect(textX, subTop, textW, sm.height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               sm.elidedText(subQ, Qt::ElideRight, textW));

    // 右侧：选中行 enter.png；常态行 ctrl.png+可见序号
    if (selected) {
        DrawEnterAction(p, r);
    } else {
        DrawShortcut(p, r, rowIdx - scrollOffset_ + 1);
    }
}

void ResultListView::DrawScrollBar(QPainter& p, const QRect& listRect) const {
    const int visible = VisibleCount();
    const int total = results_.size();
    const int range = total - visible;
    if (range <= 0) return;

    const int trackX = listRect.right() - 6;
    const int trackTop = listRect.top() + 6;
    const int trackBottom = listRect.bottom() - 6;
    const int trackH = trackBottom - trackTop;
    if (trackH <= 0) return;

    const qreal handleRatio = qreal(visible) / qreal(total);
    int handleH = int(trackH * handleRatio);
    if (handleH < 28) handleH = 28;
    if (handleH > trackH) handleH = trackH;

    const qreal pos = qreal(scrollOffset_) / qreal(range);
    const int handleY = trackTop + int((trackH - handleH) * pos);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(120, 120, 130, 96));
    p.drawRoundedRect(QRect(trackX, handleY, 4, handleH), 2, 2);
}

void ResultListView::Paint(QPainter& p, const QRect& listRect) {
    if (results_.isEmpty()) return;  // 空列表不绘制（输入框为空/无结果时窗口只保留输入框）

    const int visible = VisibleCount();
    const bool scrollable = results_.size() > kMaxVisibleRows;

    int y = listRect.top();
    for (int vi = 0; vi < visible; ++vi) {
        const int idx = scrollOffset_ + vi;
        const int rowH = VisibleRowHeight(idx);
        PaintRow(p, idx, QRect(listRect.left(), y, listRect.width(), rowH),
                 idx == hoveredIndex_);
        y += rowH;
    }
    // 规格：行间无任何缝隙或分割线 —— 故不画分隔线。

    if (scrollable) DrawScrollBar(p, listRect);
}

} // namespace iris
