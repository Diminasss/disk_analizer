#include "ui/animated_progress_bar.h"

#include <QHideEvent>
#include <QPainter>
#include <QShowEvent>
#include <QTimer>

AnimatedProgressBar::AnimatedProgressBar(QWidget* parent)
    : QProgressBar(parent),
      animation_(new QTimer(this)) {
    animation_->setInterval(35);
    connect(animation_, &QTimer::timeout, this, [this] {
        animationOffset_ = (animationOffset_ + 5) % 1000;
        update();
    });
}

void AnimatedProgressBar::hideEvent(QHideEvent* event) {
    animation_->stop();
    QProgressBar::hideEvent(event);
}

void AnimatedProgressBar::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF frame = rect().adjusted(1, 1, -1, -1);
    painter.setPen(QPen(QColor(QStringLiteral("#9caeb5")), 1));
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(frame, 3.0, 3.0);

    QRectF fill = frame.adjusted(1, 1, -1, -1);
    if (maximum() == minimum()) {
        const qreal segmentWidth = fill.width() * 0.34;
        const qreal travel = fill.width() + segmentWidth;
        fill.setX(fill.x() - segmentWidth + travel * animationOffset_ / 1000.0);
        fill.setWidth(segmentWidth);
    } else {
        const qreal ratio = static_cast<qreal>(value() - minimum()) / (maximum() - minimum());
        fill.setWidth(fill.width() * ratio);
    }

    if (fill.width() > 0) {
        painter.save();
        painter.setClipRect(frame.adjusted(1, 1, -1, -1));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#58b8d5")));
        painter.drawRoundedRect(fill, 2.0, 2.0);

        const qreal highlightX = frame.left() - 42.0
            + (frame.width() + 84.0) * animationOffset_ / 1000.0;
        painter.setBrush(QColor(255, 255, 255, 105));
        painter.drawRect(QRectF(highlightX, frame.top(), 34.0, frame.height()));
        painter.restore();
    }

    painter.setPen(Qt::black);
    const QString label = maximum() == minimum() ? format() : text();
    painter.drawText(rect(), Qt::AlignCenter, label);
}

void AnimatedProgressBar::showEvent(QShowEvent* event) {
    animation_->start();
    QProgressBar::showEvent(event);
}
