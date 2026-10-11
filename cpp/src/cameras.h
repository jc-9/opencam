#pragma once

// V4L2 camera enumeration via ioctl (no v4l2-ctl subprocess needed).

#include <QString>

#include <vector>

struct CamInfo {
    int index = -1;
    QString label;
    QString path;
    int score = 0;  // lower sorts first (USB preferred)
};

std::vector<CamInfo> listUsbCameras();
