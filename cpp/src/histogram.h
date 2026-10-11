#pragma once

// Grayscale histogram with a draggable threshold line (mirrors the Python
// HistogramWidget).

#include <QColor>
#include <QVector>
#include <QWidget>

#include "theme.h"

class HistogramWidget : public QWidget {
    Q_OBJECT
public:
    explicit HistogramWidget(const Palette& pal, QWidget* parent = nullptr);

    void setData(const QVector<int>& hist, int thresh);
    void setThreshold(int thresh);

signals:
    void thresholdChanged(int thresh);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int threshFromX(int x) const;
    int xFromThresh() const;

    QVector<int> hist_;
    int thresh_ = 128;
    bool hasData_ = false;
    bool dragging_ = false;
    QColor bg_, border_, bar_, red_;
};
