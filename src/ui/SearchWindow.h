// Iris UI —— 主搜索窗口（无边框透明 + QPainter 自绘）
// 设计依据: doc/detailed-design.md §8.3, doc/UI_demo/QPainter_Iris_Visuals.md §1/§2/§8
#pragma once

#include <memory>

#include <QWidget>
#include <QString>
#include <QVector>
#include <QTimer>

#include "provider/IProvider.h"
#include "ui/SearchBar.h"
#include "ui/ResultList.h"

class QPropertyAnimation;
class QInputMethodEvent;

namespace iris {

class SearchWindow : public QWidget {
    Q_OBJECT
public:
    explicit SearchWindow(QWidget* parent = nullptr);
    ~SearchWindow() override;

    void showWithFadeIn();
    void hideWithFadeOut();

    /// 弹应用自身模态对话框（关于/警告）期间置 true：阻止失焦自动隐藏。
    /// 否则对话框夺焦 → hideWithFadeOut，本窗口在模态恢复激活时崩溃。
    void SetSuppressAutoHide(bool suppress) { suppressAutoHide_ = suppress; }

signals:
    void searchRequested(const QString& text);
    void itemActivated(const ResultItem& item);

public slots:
    void onSearchFinished(const QVector<ResultItem>& results);

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void inputMethodEvent(QInputMethodEvent* event) override;  // 接收 IME 提交文本，支持中文输入
    void contextMenuEvent(QContextMenuEvent* event) override;  // 结果项右键菜单

private:
    void RebuildLayout();
    QRect GetInputRect() const;
    QRect GetListRect() const;
    void OpenSelected();
    void OpenByVisibleNo(int visibleNo);  // Ctrl+1..9：打开第N可见行（对应右侧 ctrlN 提示）
    void EnsureEnglishInput();           // 弹出时关闭 TSF IME，默认英文输入
    void QueryAndSaveImeState();         // setFocus 前保存系统 IME 原状态
    void RestoreIme();                   // 隐藏时恢复弹出前的 IME 状态

    SearchBar     searchBar_;
    ResultListView resultList_;

    QString inputText_;
    QString preeditText_;              // IME 预编辑文本（拼音实时预览）
    QTimer  cursorTimer_;
    bool    cursorVisible_ = true;
    bool    suppressAutoHide_ = false;  // 弹模态对话框期间抑制失焦自动隐藏
    bool    imeWasOpen_ = false;         // 弹出前 IME 是否开启（隐藏时据此恢复）
    bool    imeStateSaved_ = false;      // 防重入：EnsureEnglishInput 多次调用只保存首次

    static constexpr int kWindowWidth  = 1440;
    static constexpr int kInputHeight   = SearchBar::kHeight;   // 100
    static constexpr int kCornerRadius  = 16;
    static constexpr int kShadowMargin  = 28;  // 内容内缩边距，为柔和外阴影留出边距带
    static constexpr int kResizeBorder  = 8;
};

} // namespace iris
