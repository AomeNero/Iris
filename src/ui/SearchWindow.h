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

namespace iris {

class SearchWindow : public QWidget {
    Q_OBJECT
public:
    explicit SearchWindow(QWidget* parent = nullptr);
    ~SearchWindow() override;

    void showWithFadeIn();
    void hideWithFadeOut();

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

private:
    void RebuildLayout();
    QRect GetInputRect() const;
    QRect GetListRect() const;
    void OpenSelected();

    SearchBar     searchBar_;
    ResultListView resultList_;

    QString inputText_;
    QTimer  cursorTimer_;
    bool    cursorVisible_ = true;

    static constexpr int kWindowWidth  = 1440;
    static constexpr int kInputHeight   = SearchBar::kHeight;   // 100
    static constexpr int kCornerRadius  = 16;
    static constexpr int kShadowMargin  = 28;  // 内容内缩边距，为柔和外阴影留出边距带
    static constexpr int kResizeBorder  = 8;
};

} // namespace iris
