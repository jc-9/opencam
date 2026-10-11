#pragma once

// Aspect-preserving live view (mirrors the Python VideoDisplay).

#include <QImage>
#include <QPixmap>
#include <QWidget>

#include <memory>

#include "theme.h"

class VideoDisplay : public QWidget {
    Q_OBJECT
public:
    explicit VideoDisplay(const Palette& pal, QWidget* parent = nullptr);

    void setFrame(const QImage& img);
    bool hasFrame() const { return pixmap_ && !pixmap_->isNull(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::unique_ptr<QPixmap> pixmap_;
    QString placeholder_;
    QColor bg_, border_, muted_;
};
