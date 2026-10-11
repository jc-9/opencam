#include "filters.h"

#include <opencv2/geometry/2d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>

const std::vector<FilterChoice>& availableFilters() {
    static const std::vector<FilterChoice> kFilters = {
        {QStringLiteral("Binary Threshold"), QStringLiteral("binary")},
        {QStringLiteral("Gaussian Smoothing"), QStringLiteral("gaussian")},
        {QStringLiteral("Sobel X"), QStringLiteral("sobelx")},
        {QStringLiteral("Image Color"), QStringLiteral("color")},
        {QStringLiteral("Morphological"), QStringLiteral("morph")},
        {QStringLiteral("Canny"), QStringLiteral("canny")},
        {QStringLiteral("Median Blur"), QStringLiteral("median")},
        {QStringLiteral("Bilateral Filter"), QStringLiteral("bilateral")},
        {QStringLiteral("Color Threshold"), QStringLiteral("color_threshold")},
        {QStringLiteral("Rotate Image"), QStringLiteral("rotate")},
    };
    return kFilters;
}

FilterParams defaultParams(const QString& key) {
    FilterParams p;
    if (key == QStringLiteral("binary")) p.thresh = 128;
    else if (key == QStringLiteral("gaussian")) { p.kernel = 5; p.passes = 1; }
    else if (key == QStringLiteral("sobelx")) p.kernel = 3;
    else if (key == QStringLiteral("color")) p.mode = QStringLiteral("color");
    else if (key == QStringLiteral("morph")) {
        p.operation = QStringLiteral("OPEN");
        p.kernelShape = QStringLiteral("RECT");
        p.kernelSize = 5; p.iterations = 1;
    } else if (key == QStringLiteral("canny")) {
        p.threshold1 = 50; p.threshold2 = 150; p.aperture = 3; p.l2 = false;
    } else if (key == QStringLiteral("median")) p.kernelSize = 5;
    else if (key == QStringLiteral("bilateral")) { p.d = 9; p.sigmaColor = 75.0; p.sigmaSpace = 75.0; }
    else if (key == QStringLiteral("color_threshold")) {
        p.colorSpace = QStringLiteral("HSV");
        p.lowerH = 0; p.lowerS = 0; p.lowerV = 0;
        p.upperH = 180; p.upperS = 255; p.upperV = 255; p.invert = false;
    } else if (key == QStringLiteral("rotate")) {
        p.angle = 0;
    }
    return p;
}

namespace {

int oddOr(int k, int fallback) { return (k % 2 == 0) ? fallback : k; }

cv::Mat gray2bgr(const cv::Mat& gray) {
    cv::Mat out;
    cv::cvtColor(gray, out, cv::COLOR_GRAY2BGR);
    return out;
}

}  // namespace

cv::Mat applyFilter(const cv::Mat& frame, const QString& filt, const FilterParams& p) {
    if (frame.empty() || filt == QStringLiteral("none")) return frame.clone();

    if (filt == QStringLiteral("color")) {
        if (p.mode == QStringLiteral("gray")) {
            cv::Mat gray;
            cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
            return gray2bgr(gray);
        }
        return frame.clone();
    }

    if (filt == QStringLiteral("color_threshold")) {
        cv::Mat conv;
        cv::Vec3b lo, hi;
        if (p.colorSpace == QStringLiteral("HSV")) {
            cv::cvtColor(frame, conv, cv::COLOR_BGR2HSV);
            lo = { (uchar)p.lowerH, (uchar)p.lowerS, (uchar)p.lowerV };
            hi = { (uchar)p.upperH, (uchar)p.upperS, (uchar)p.upperV };
        } else if (p.colorSpace == QStringLiteral("LAB")) {
            cv::cvtColor(frame, conv, cv::COLOR_BGR2Lab);
            lo = { (uchar)p.lowerH, (uchar)p.lowerS, (uchar)p.lowerV };
            hi = { (uchar)p.upperH, (uchar)p.upperS, (uchar)p.upperV };
        } else {
            cv::cvtColor(frame, conv, cv::COLOR_BGR2YCrCb);
            lo = { (uchar)p.lowerH, (uchar)p.lowerS, (uchar)p.lowerV };
            hi = { (uchar)p.upperH, (uchar)p.upperS, (uchar)p.upperV };
        }
        cv::Mat mask;
        cv::inRange(conv, lo, hi, mask);
        if (p.invert) mask = 255 - mask;
        return gray2bgr(mask);
    }

    if (filt == QStringLiteral("rotate")) {
        const int norm = ((p.angle % 360) + 360) % 360;
        if (norm == 0) return frame;  // neutral: passthrough (never mutated downstream)
        cv::Mat out;
        if (norm == 180) { cv::rotate(frame, out, cv::ROTATE_180); return out; }
        if (norm == 90) { cv::rotate(frame, out, cv::ROTATE_90_COUNTERCLOCKWISE); return out; }
        if (norm == 270) { cv::rotate(frame, out, cv::ROTATE_90_CLOCKWISE); return out; }
        const cv::Point2f center(frame.cols / 2.0f, frame.rows / 2.0f);
        const cv::Mat m = cv::getRotationMatrix2D(center, p.angle, 1.0);
        cv::warpAffine(frame, out, m, frame.size());
        return out;
    }

    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    if (filt == QStringLiteral("binary")) {
        cv::Mat binary;
        cv::threshold(gray, binary, p.thresh, 255, cv::THRESH_BINARY);
        return gray2bgr(binary);
    }
    if (filt == QStringLiteral("gaussian")) {
        const int k = oddOr(p.kernel, 5);
        cv::Mat b = gray;
        for (int i = 0; i < p.passes; ++i) cv::GaussianBlur(b, b, cv::Size(k, k), 0);
        return gray2bgr(b);
    }
    if (filt == QStringLiteral("sobelx")) {
        const int k = oddOr(p.kernel, 3);
        cv::Mat sob, abs8;
        cv::Sobel(gray, sob, CV_64F, 1, 0, k);
        cv::convertScaleAbs(sob, abs8);
        return gray2bgr(abs8);
    }
    if (filt == QStringLiteral("morph")) {
        static const std::vector<std::pair<QString, int>> kOps = {
            {QStringLiteral("ERODE"), cv::MORPH_ERODE}, {QStringLiteral("DILATE"), cv::MORPH_DILATE},
            {QStringLiteral("OPEN"), cv::MORPH_OPEN}, {QStringLiteral("CLOSE"), cv::MORPH_CLOSE},
            {QStringLiteral("GRADIENT"), cv::MORPH_GRADIENT}, {QStringLiteral("TOPHAT"), cv::MORPH_TOPHAT},
            {QStringLiteral("BLACKHAT"), cv::MORPH_BLACKHAT},
        };
        int op = cv::MORPH_OPEN;
        for (const auto& [name, code] : kOps)
            if (p.operation == name) { op = code; break; }
        int shape = cv::MORPH_RECT;
        if (p.kernelShape == QStringLiteral("ELLIPSE")) shape = cv::MORPH_ELLIPSE;
        else if (p.kernelShape == QStringLiteral("CROSS")) shape = cv::MORPH_CROSS;
        const int ksize = oddOr(p.kernelSize, 5);
        const cv::Mat kernel = cv::getStructuringElement(shape, cv::Size(ksize, ksize));
        cv::Mat res;
        cv::morphologyEx(gray, res, op, kernel, cv::Point(-1, -1), p.iterations);
        return gray2bgr(res);
    }
    if (filt == QStringLiteral("canny")) {
        cv::Mat edges;
        cv::Canny(gray, edges, p.threshold1, p.threshold2, p.aperture, p.l2);
        return gray2bgr(edges);
    }
    if (filt == QStringLiteral("median")) {
        const int k = oddOr(p.kernelSize, 5);
        cv::Mat m;
        cv::medianBlur(gray, m, k);
        return gray2bgr(m);
    }
    if (filt == QStringLiteral("bilateral")) {
        cv::Mat b;
        cv::bilateralFilter(gray, b, p.d, p.sigmaColor, p.sigmaSpace);
        return gray2bgr(b);
    }
    return frame.clone();
}

PipeResult applyPipeline(const cv::Mat& frame, const std::vector<FilterStage>& pipe,
                         int selected, int upto) {
    PipeResult r;
    if (frame.empty()) return r;
    size_t n = pipe.size();
    if (upto >= 0) n = std::min<size_t>(n, static_cast<size_t>(upto) + 1);
    cv::Mat current = frame.clone();
    for (size_t i = 0; i < n; ++i) {
        const FilterStage& f = pipe[i];
        if (static_cast<int>(i) == selected && f.type == QStringLiteral("binary")) {
            cv::Mat gray;
            cv::cvtColor(current, gray, cv::COLOR_BGR2GRAY);
            const int channels[] = {0};
            const int histSize[] = {256};
            const float range[] = {0, 256};
            const float* ranges[] = {range};
            cv::Mat hist;
            cv::calcHist(&gray, 1, channels, cv::Mat(), hist, 1, histSize, ranges);
            r.hist.assign(256, 0);
            for (int b = 0; b < 256; ++b) r.hist[b] = static_cast<int>(hist.at<float>(b));
            r.thresh = f.params.thresh;
            r.hasHist = true;
        }
        current = applyFilter(current, f.type, f.params);
    }
    r.display = current;
    return r;
}
