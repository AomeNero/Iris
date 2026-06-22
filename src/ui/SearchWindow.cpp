// Iris UI —— SearchWindow 实现
#include "ui/SearchWindow.h"
#include "ui/Theme.h"

#include "core/WinUtil.h"
#include "core/StringUtil.h"

#include <QPainter>
#include <QPainterPath>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QPropertyAnimation>
#include <QInputMethodEvent>
#include <QApplication>
#include <QRect>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <imm.h>           // Imm* 输入法状态（弹出强制英文 / 隐藏恢复）

namespace iris {

SearchWindow::SearchWindow(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_InputMethodEnabled, true);  // 启用输入法，支持中文 IME 输入

    setMouseTracking(true);  // 悬停高亮需要无按钮鼠标移动事件

    connect(&cursorTimer_, &QTimer::timeout, this, [this]() {
        cursorVisible_ = !cursorVisible_;
        update();
    });
    cursorTimer_.setInterval(530);
    cursorTimer_.start();

    RebuildLayout();
    setFocusPolicy(Qt::StrongFocus);
}

SearchWindow::~SearchWindow() = default;

void SearchWindow::RebuildLayout() {
    const int listH = resultList_.CalculateHeight();
    setFixedSize(kWindowWidth + 2 * kShadowMargin,
                 kInputHeight + listH + 2 * kShadowMargin);
}

QRect SearchWindow::GetInputRect() const {
    return QRect(kShadowMargin, kShadowMargin, kWindowWidth, kInputHeight);
}
QRect SearchWindow::GetListRect() const {
    return QRect(kShadowMargin, kShadowMargin + kInputHeight,
                 kWindowWidth, resultList_.CalculateHeight());
}

void SearchWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 内容区（内缩 kShadowMargin，作为内容到窗口边的留白带）
    const QRect content(kShadowMargin, kShadowMargin, kWindowWidth,
                        height() - 2 * kShadowMargin);

    // 裁剪到内容圆角区，后续绘制全部落在内容区内
    QPainterPath contentPath;
    contentPath.addRoundedRect(content, kCornerRadius, kCornerRadius);
    p.setClipPath(contentPath);

    // Layer 1: 灰基底 #e9e9e9（已移除原白底配套的顶部光泽——灰底上白光泽会把
    //         背景拉向白色、偏离 #e9e9e9）
    p.fillRect(content, CurrentPalette().base);

    // Layer 3-4: 输入栏
    const QRect inputRect = GetInputRect();
    searchBar_.Paint(p, inputRect, inputText_, preeditText_, cursorVisible_, hasFocus());

    // Layer 5: 结果列表
    resultList_.Paint(p, GetListRect());

    // Layer 6: 极细边框
    p.setClipping(false);
    QPen borderPen(CurrentPalette().border);
    borderPen.setWidthF(0.5);
    p.setPen(borderPen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(content.adjusted(0, 0, -1, -1), kCornerRadius, kCornerRadius);
}

void SearchWindow::showWithFadeIn() {
    inputText_.clear();
    preeditText_.clear();
    resultList_.Clear();
    RebuildLayout();

    // 居中到当前显示器
    const RECT mr = WinUtil::GetCurrentMonitorRect();
    const int x = mr.left + (mr.right - mr.left - width()) / 2;
    const int y = mr.top + (mr.bottom - mr.top - height()) / 3;  // 偏上 1/3
    move(x, y);

    setWindowOpacity(0.0);
    show();
    raise();
    activateWindow();
    setFocus();

    // 弹出时强制英文输入模式（便于搜索文件名/命令），记录原状态供隐藏时恢复
    if (HWND hwnd = reinterpret_cast<HWND>(winId())) {
        if (HIMC himc = ImmGetContext(hwnd)) {
            if (ImmGetConversionStatus(himc, &imeConversion_, &imeSentence_)) {
                imeSaved_ = true;
                ImmSetConversionStatus(himc, imeConversion_ & ~IME_CMODE_NATIVE, imeSentence_);
            }
            ImmReleaseContext(hwnd, himc);
        }
    }

    auto* anim = new QPropertyAnimation(this, "windowOpacity", this);
    anim->setDuration(180);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void SearchWindow::hideWithFadeOut() {
    // 恢复弹出前的 IME 输入模式（弹出时曾强制切英文）
    if (imeSaved_) {
        if (HWND hwnd = reinterpret_cast<HWND>(winId())) {
            if (HIMC himc = ImmGetContext(hwnd)) {
                ImmSetConversionStatus(himc, imeConversion_, imeSentence_);
                ImmReleaseContext(hwnd, himc);
            }
        }
        imeSaved_ = false;
    }
    preeditText_.clear();

    auto* anim = new QPropertyAnimation(this, "windowOpacity", this);
    anim->setDuration(150);
    anim->setStartValue(windowOpacity());
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::InCubic);
    connect(anim, &QPropertyAnimation::finished, this, &QWidget::hide);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void SearchWindow::onSearchFinished(const QVector<ResultItem>& results) {
    resultList_.SetEmptyHint(inputText_.isEmpty()
        ? QString::fromUtf8("开始输入以搜索")
        : QString::fromUtf8("无搜索结果"));
    resultList_.SetResults(results);
    RebuildLayout();
    update();
}

void SearchWindow::OpenSelected() {
    if (const ResultItem* item = resultList_.GetSelected()) {
        emit itemActivated(*item);
        hideWithFadeOut();
    }
}

void SearchWindow::OpenByVisibleNo(int visibleNo) {
    if (const ResultItem* item = resultList_.GetVisibleItem(visibleNo)) {
        emit itemActivated(*item);
        hideWithFadeOut();
    }
}

void SearchWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Escape:
            hideWithFadeOut();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (event->modifiers() & Qt::ControlModifier) {
                // Ctrl+Enter：打开所在文件夹（P2 完善；此处仅隐藏）
                hideWithFadeOut();
            } else {
                OpenSelected();
            }
            return;
        case Qt::Key_Up:
            resultList_.MoveSelectionUp();
            update();
            return;
        case Qt::Key_Down:
            resultList_.MoveSelectionDown();
            update();
            return;
        case Qt::Key_1: case Qt::Key_2: case Qt::Key_3:
        case Qt::Key_4: case Qt::Key_5: case Qt::Key_6:
        case Qt::Key_7: case Qt::Key_8: case Qt::Key_9:
            if (event->modifiers() & Qt::ControlModifier) {
                OpenByVisibleNo(event->key() - Qt::Key_0);  // ctrlN 提示对应 Ctrl+N
                return;
            }
            break;  // 非 Ctrl：作为数字输入字符，落到下方追加逻辑
        case Qt::Key_Backspace:
            if (!inputText_.isEmpty()) {
                inputText_.chop(1);
                emit searchRequested(inputText_);
                update();
            }
            return;
        default:
            break;
    }
    if (event->text().isEmpty()) {
        QWidget::keyPressEvent(event);
        return;
    }
    // 追加可打印字符（仅 ASCII 控制外的可见字符）
    for (const QChar ch : event->text()) {
        if (ch.isPrint()) inputText_ += ch;
    }
    emit searchRequested(inputText_);
    update();
}

void SearchWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const int y = event->pos().y() - kShadowMargin;
    if (y >= kInputHeight) {
        resultList_.SelectByY(y - kInputHeight);
        update();
        OpenSelected();
    }
}

void SearchWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const int y = event->pos().y() - kShadowMargin;
    if (y >= kInputHeight) {
        resultList_.SelectByY(y - kInputHeight);
        OpenSelected();
    }
}

void SearchWindow::mouseMoveEvent(QMouseEvent* event) {
    const int y = event->pos().y() - kShadowMargin;
    // 悬停即选中（仅在列表区域内）；移出列表区不改变选中
    if (y >= kInputHeight) {
        if (resultList_.SetHoverByY(y - kInputHeight)) update();
    }
}

void SearchWindow::wheelEvent(QWheelEvent* event) {
    const int steps = event->angleDelta().y() / 120;
    if (steps != 0) {
        resultList_.ScrollBy(-steps);  // 向上滚 → 选择上移
        update();
    }
}

void SearchWindow::focusOutEvent(QFocusEvent* event) {
    // 失焦（点击外部其他窗口）淡出隐藏；但以下不隐藏：
    //  - 焦点转给弹出菜单（PopupFocusReason，如托盘右键菜单）：
    //    否则点菜单项弹对话框时本窗口早已被隐藏，对话框关闭后无法恢复焦点
    //  - 弹应用自身模态对话框期间（suppressAutoHide_ 守卫）
    if (!suppressAutoHide_
        && event->reason() != Qt::PopupFocusReason
        && !isHidden()) {
        hideWithFadeOut();
    }
    QWidget::focusOutEvent(event);
}

bool SearchWindow::nativeEvent(const QByteArray& /*type*/, void* message, long* result) {
#ifdef Q_OS_WIN
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_NCHITTEST) {
        const LONG border = kResizeBorder;
        RECT wr;
        GetWindowRect(HWND(winId()), &wr);
        const LONG x = GET_X_LPARAM(msg->lParam);
        const LONG y = GET_Y_LPARAM(msg->lParam);
        if (x < wr.left + border && y < wr.top + border)  { *result = HTTOPLEFT;     return true; }
        if (x > wr.right - border && y < wr.top + border)  { *result = HTTOPRIGHT;    return true; }
        if (x < wr.left + border && y > wr.bottom - border){ *result = HTBOTTOMLEFT;  return true; }
        if (x > wr.right - border && y > wr.bottom - border){*result = HTBOTTOMRIGHT; return true; }
        if (x < wr.left + border)   { *result = HTLEFT;   return true; }
        if (x > wr.right - border)  { *result = HTRIGHT;  return true; }
        if (y < wr.top + border)    { *result = HTTOP;    return true; }
        if (y > wr.bottom - border) { *result = HTBOTTOM; return true; }
    }
#endif
    return false;
}

QVariant SearchWindow::inputMethodQuery(Qt::InputMethodQuery query) const {
    if (query == Qt::ImCursorRectangle) {
        const QFontMetrics fm(searchBar_.Font());
        const int textW = fm.horizontalAdvance(inputText_) + fm.horizontalAdvance(preeditText_);
        // 与 SearchBar::Paint 一致：box 左/上/下缩进 kPadH，右缩进 (40+iconSize) 为 iris 让位
        const int iconSize = kInputHeight - 2 * SearchBar::kPadH - 8;
        const QRect box = GetInputRect().adjusted(SearchBar::kPadH, SearchBar::kPadH,
                                                   -(40 + iconSize), -SearchBar::kPadH);
        const int cursorH = box.height() - 16;
        const int textRight = box.right() - SearchBar::kPadH;
        const int x = qMin(box.left() + SearchBar::kPadH + textW, textRight);
        const int y = box.top() + (box.height() - cursorH) / 2;
        return QRect(x, y, SearchBar::kCursorW, cursorH);
    }
    return QWidget::inputMethodQuery(query);
}

void SearchWindow::inputMethodEvent(QInputMethodEvent* e) {
    // IME 提交文本（中文输入法选字后）：追加到输入框，与 keyPressEvent 的字符追加一致
    if (!e->commitString().isEmpty()) {
        inputText_ += e->commitString();
        emit searchRequested(inputText_);
    }
    // 预编辑文本（拼音实时预览）；提交时 IME 会清空 preeditString → preeditText_ 自动清空
    preeditText_ = e->preeditString();
    e->accept();
    update();
}

} // namespace iris
