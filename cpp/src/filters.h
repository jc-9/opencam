#pragma once

// Pure-OpenCV filter chain. No Qt Widgets dependency (only QString for
// filter keys, QMetaType so stages can travel in signals).

#include <opencv2/core.hpp>

#include <QMetaType>
#include <QString>

#include <utility>
#include <vector>

struct FilterParams {
    int thresh = 128;
    int kernel = 5;
    int passes = 1;
    QString mode = QStringLiteral("color");
    QString operation = QStringLiteral("OPEN");
    QString kernelShape = QStringLiteral("RECT");
    int kernelSize = 5;
    int iterations = 1;
    int threshold1 = 50;
    int threshold2 = 150;
    int aperture = 3;
    bool l2 = false;
    int d = 9;
    double sigmaColor = 75.0;
    double sigmaSpace = 75.0;
    QString colorSpace = QStringLiteral("HSV");
    int lowerH = 0, lowerS = 0, lowerV = 0;
    int upperH = 180, upperS = 255, upperV = 255;
    bool invert = false;
    int angle = 0;  // rotate, degrees counter-clockwise
};
Q_DECLARE_METATYPE(FilterParams)

struct FilterStage {
    QString type;
    QString name;
    FilterParams params;
};
Q_DECLARE_METATYPE(FilterStage)

struct PipeResult {
    cv::Mat display;             // BGR, 3 channels (empty on null input)
    bool hasHist = false;        // true when the selected stage is binary
    std::vector<int> hist;       // 256 bins of the binary stage's input
    int thresh = 128;
};

struct FilterChoice {
    QString label;
    QString key;
};

// All addable filters (excludes "none").
const std::vector<FilterChoice>& availableFilters();
FilterParams defaultParams(const QString& key);

// Single-stage application. `frame` is never mutated.
cv::Mat applyFilter(const cv::Mat& frame, const QString& filt, const FilterParams& p);

// Full chain. Applies stages [0..upto] (upto < 0 = whole chain) so a UI
// selection can preview the cumulative result 1..N.
PipeResult applyPipeline(const cv::Mat& frame, const std::vector<FilterStage>& pipe,
                         int selected, int upto = -1);
