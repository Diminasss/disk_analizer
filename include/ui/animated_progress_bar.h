#pragma once

#include <QProgressBar>

class QHideEvent;
class QPaintEvent;
class QShowEvent;
class QTimer;

class AnimatedProgressBar final : public QProgressBar {
public:
    explicit AnimatedProgressBar(QWidget* parent = nullptr);

protected:
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QTimer* animation_{nullptr};
    int animationOffset_{0};
};
