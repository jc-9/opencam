#include "cameras.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>

namespace {

int scoreCam(const QString& card, const QString& bus) {
    const QString c = card.toLower();
    const QString b = bus.toLower();
    int s = 0;
    if (b.contains(QStringLiteral("usb"))) s += 10;
    for (const char* k : {"hd", "c-mount", "webcam", "cam", "usb"}) {
        if (c.contains(QLatin1String(k))) { s += 5; break; }
    }
    if (c.contains(QStringLiteral("integrated")) || c.contains(QStringLiteral("internal"))) s -= 5;
    return -s;  // ascending sort puts best first
}

}  // namespace

std::vector<CamInfo> listUsbCameras() {
    std::vector<CamInfo> out;
    for (int i = 0; i < 64; ++i) {
        char dev[32];
        std::snprintf(dev, sizeof(dev), "/dev/video%d", i);
        const int fd = ::open(dev, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        struct v4l2_capability cap = {};
        bool ok = ::ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0;
        ::close(fd);
        if (!ok) continue;
        const QString card = QString::fromUtf8(reinterpret_cast<const char*>(cap.card));
        const QString bus = QString::fromUtf8(reinterpret_cast<const char*>(cap.bus_info));
        CamInfo info;
        info.index = i;
        info.label = QStringLiteral("%1: %2").arg(i).arg(card);
        info.path = QString::fromLatin1(dev);
        info.score = scoreCam(card, bus);
        out.push_back(info);
    }
    // Prefer USB devices; keep stable order otherwise.
    std::stable_sort(out.begin(), out.end(), [](const CamInfo& a, const CamInfo& b) {
        if (a.score != b.score) return a.score < b.score;
        return a.index < b.index;
    });
    return out;
}
