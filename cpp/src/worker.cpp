#include "worker.h"

#include <opencv2/imgproc.hpp>

#include <QElapsedTimer>
#include <QMutexLocker>
#include <QThread>

namespace {

double nowSeconds() {
    static QElapsedTimer t;
    if (!t.isValid()) t.start();
    return t.elapsed() / 1000.0;
}

QImage frameToQImage(const cv::Mat& frame) {
    if (frame.empty()) return QImage();
    cv::Mat rgb;
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step),
                  QImage::Format_RGB888)
        .copy();
}

}  // namespace

CameraWorker::CameraWorker(int camIndex, std::vector<FilterStage> pipeline, int selected,
                           QObject* parent)
    : QObject(parent), camIndex_(camIndex), pipeline_(std::move(pipeline)), selected_(selected) {}

void CameraWorker::updateSettings(int camIndex, std::vector<FilterStage> pipeline, int selected) {
    QMutexLocker lock(&mutex_);
    const bool needRestart = camIndex != camIndex_;
    camIndex_ = camIndex;
    pipeline_ = std::move(pipeline);
    selected_ = selected;
    if (needRestart && running_) restartCapture();
}

void CameraWorker::restartCapture() {
    if (cap_.isOpened()) cap_.release();
    openCapture();
}

bool CameraWorker::openCapture() {
    if (!cap_.open(camIndex_, cv::CAP_V4L2)) {
        emit error(QStringLiteral("Failed to open camera %1").arg(camIndex_));
        return false;
    }
    cap_.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
    cap_.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
    cap_.set(cv::CAP_PROP_FPS, 30);
    cap_.set(cv::CAP_PROP_BUFFERSIZE, 1);
    return true;
}

void CameraWorker::stop() {
    QMutexLocker lock(&mutex_);
    running_ = false;
}

void CameraWorker::run() {
    {
        QMutexLocker lock(&mutex_);
        running_ = true;
    }
    if (!openCapture()) {
        QMutexLocker lock(&mutex_);
        running_ = false;
        emit finished();
        return;
    }
    lastFpsTime_ = nowSeconds();
    lastFpsCount_ = frames_;

    while (true) {
        {
            QMutexLocker lock(&mutex_);
            if (!running_) break;
        }
        cv::Mat frame;
        if (!cap_.read(frame) || frame.empty()) {
            QThread::msleep(50);
            continue;
        }

        std::vector<FilterStage> pipe;
        int sel = -1;
        {
            QMutexLocker lock(&mutex_);
            pipe = pipeline_;
            sel = selected_;
            lastRaw_ = frame;  // frame is never mutated downstream
        }
        const int upto = (sel >= 0 && sel < static_cast<int>(pipe.size())) ? sel : -1;
        const PipeResult r = applyPipeline(frame, pipe, sel, upto);

        HistData hist;
        if (r.hasHist) {
            hist.valid = true;
            hist.bins = QVector<int>(r.hist.begin(), r.hist.end());
            hist.thresh = r.thresh;
        }

        ++frames_;
        const double now = nowSeconds();
        if (now - lastFpsTime_ >= 1.0) {
            const double fps = (frames_ - lastFpsCount_) / (now - lastFpsTime_);
            emit fpsReady(fps);
            lastFpsTime_ = now;
            lastFpsCount_ = frames_;
        }

        emit frameReady(frameToQImage(r.display), hist);
        QThread::msleep(10);
    }

    if (cap_.isOpened()) cap_.release();
    emit finished();
}

cv::Mat CameraWorker::lastRaw() {
    QMutexLocker lock(&mutex_);
    return lastRaw_;
}
