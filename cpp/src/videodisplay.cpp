#include "videodisplay.h"

#include <QFont>
#include <QPainter>
#include <QPen>

VideoDisplay::VideoDisplay(const Palette& pal, QWidget* parent)
    : QWidget(parent),
      placeholder_(QStringLiteral("No stream — select camera and press Start")),
      bg_(pal.darkBg),
      border_(pal.border),
      muted_(pal.muted) {
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setStyleSheet(QStringLiteral("background: %1; border: 1px solid %2; border-radius: 6px;")
                      .arg(pal.darkBg, pal.border));
}

void VideoDisplay::setFrame(const QImage& img) {
    if (img.isNull()) {
        pixmap_.reset();
    } else {
        pixmap_ = std::make_unique<QPixmap>(QPixmap::fromImage(img));
    }
    update();
}

void VideoDisplay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRect r = rect();
    p.fillRect(r, bg_);
    if (pixmap_ && !pixmap_->isNull()) {
        const QSize fit = pixmap_->size().scaled(r.size(), Qt::KeepAspectRatio);
        const int x = (r.width() - fit.width()) / 2;
        const int y = (r.height() - fit.height()) / 2;
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawPixmap(x, y, pixmap_->scaled(fit, Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation));
    } else {
        p.setPen(muted_);
        p.setFont(QFont(QStringLiteral("monospace"), 11));
        p.drawText(r, Qt::AlignCenter, placeholder_);
    }
    p.setPen(QPen(border_, 1));
    p.drawRect(r.adjusted(1, 1, -1, -1));
}
