#include "histogram.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cstdlib>

HistogramWidget::HistogramWidget(const Palette& pal, QWidget* parent)
    : QWidget(parent),
      bg_(pal.darkBg),
      border_(pal.border),
      bar_(pal.histBar),
      red_(pal.redLine) {
    setFixedHeight(118);
    setMinimumWidth(200);
    setStyleSheet(QStringLiteral("background: %1; border: 1px solid %2; border-radius: 4px;")
                      .arg(pal.darkBg, pal.border));
}

void HistogramWidget::setData(const QVector<int>& hist, int thresh) {
    hist_ = hist;
    thresh_ = qBound(0, thresh, 255);
    hasData_ = true;
    update();
}

void HistogramWidget::setThreshold(int thresh) {
    thresh_ = qBound(0, thresh, 255);
    update();
}

int HistogramWidget::xFromThresh() const {
    return static_cast<int>((thresh_ / 255.0) * (width() - 1));
}

int HistogramWidget::threshFromX(int x) const {
    if (width() <= 1) return 0;
    return qBound(0, static_cast<int>(x * 255.0 / (width() - 1) + 0.5), 255);
}

void HistogramWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), bg_);
    if (hasData_ && !hist_.isEmpty()) {
        int peak = 1;
        for (int v : hist_) peak = std::max(peak, v);
        const int n = hist_.size();
        const double bw = static_cast<double>(width()) / n;
        p.setPen(Qt::NoPen);
        p.setBrush(bar_);
        for (int i = 0; i < n; ++i) {
            const int bh = static_cast<int>(hist_[i] * (height() - 8) / peak);
            const int bx = static_cast<int>(i * bw);
            p.drawRect(bx, height() - 4 - bh, std::max(1, static_cast<int>(bw + 0.5)), bh);
        }
    }
    const int x = xFromThresh();
    p.setPen(QPen(red_, 2));
    p.drawLine(x, 2, x, height() - 2);
    p.setPen(QPen(border_, 1));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}

void HistogramWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const int x = event->pos().x();
    if (std::abs(x - xFromThresh()) <= 6) {
        dragging_ = true;
    } else {
        thresh_ = threshFromX(x);
        update();
        emit thresholdChanged(thresh_);
    }
}

void HistogramWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) return;
    thresh_ = threshFromX(event->pos().x());
    update();
    emit thresholdChanged(thresh_);
}

void HistogramWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) dragging_ = false;
}
