#pragma once

// Capture + filter worker living on a QThread (mirrors the Python QThread worker).

#include <opencv2/videoio.hpp>

#include <QImage>
#include <QMetaType>
#include <QMutex>
#include <QObject>
#include <QVector>

#include "filters.h"

struct HistData {
    bool valid = false;
    QVector<int> bins;
    int thresh = 128;
};
Q_DECLARE_METATYPE(HistData)

class CameraWorker : public QObject {
    Q_OBJECT
public:
    explicit CameraWorker(int camIndex, std::vector<FilterStage> pipeline, int selected,
                          QObject* parent = nullptr);

public slots:
    void run();
    void stop();
    void updateSettings(int camIndex, std::vector<FilterStage> pipeline, int selected);

signals:
    void frameReady(QImage img, HistData hist);
    void fpsReady(double fps);
    void error(QString msg);
    void finished();

private:
    bool openCapture();
    void restartCapture();

    int camIndex_;
    std::vector<FilterStage> pipeline_;
    int selected_ = -1;
    bool running_ = false;
    QMutex mutex_;
    cv::VideoCapture cap_;
    cv::Mat lastRaw_;  // guarded by mutex_ (written every frame, read after stop)
    long frames_ = 0;
    double lastFpsTime_ = 0.0;
    long lastFpsCount_ = 0;

public:
    cv::Mat lastRaw();
};
