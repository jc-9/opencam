// opencam C++ port — entry point, theme palette, optional --self-test
// (filter-chain benchmark + assertions, no camera needed).

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFont>
#include <QPalette>
#include <QString>
#include <QTextStream>

#include <cstdio>

#include "filters.h"
#include "mainwindow.h"
#include "theme.h"
#include "version.h"

namespace {

cv::Mat makeGradient(int w = 1280, int h = 720) {
    cv::Mat gray(h, w, CV_8UC1);
    for (int y = 0; y < h; ++y) {
        uchar* row = gray.ptr<uchar>(y);
        for (int x = 0; x < w; ++x) row[x] = static_cast<uchar>(x * 255 / (w - 1));
    }
    cv::Mat bgr;
    cv::cvtColor(gray, bgr, cv::COLOR_GRAY2BGR);
    return bgr;
}

int selfTest() {
    QTextStream out(stdout);
    const cv::Mat frame = makeGradient();
    std::vector<FilterStage> pipe;
    for (const FilterChoice& fc : availableFilters()) {
        FilterStage s;
        s.type = fc.key;
        s.name = fc.label;
        s.params = defaultParams(fc.key);
        pipe.push_back(s);
    }

    // Correctness assertions.
    {
        FilterParams bp;
        bp.thresh = 128;
        const cv::Mat bin = applyFilter(frame, QStringLiteral("binary"), bp);
        if (bin.empty() || bin.channels() != 3) {
            out << "SELFTEST_FAIL binary shape\n";
            return 1;
        }
        const cv::Mat gray = [bin] { cv::Mat g; cv::cvtColor(bin, g, cv::COLOR_BGR2GRAY); return g; }();
        double mn = 0, mx = 0;
        cv::minMaxLoc(gray, &mn, &mx);
        if (mn != 0.0 || mx != 255.0) {
            out << "SELFTEST_FAIL binary values\n";
            return 1;
        }
    }
    {
        // pipe[0] is binary: selecting it must yield a histogram of its input.
        const PipeResult r = applyPipeline(frame, pipe, 0, 1);
        if (r.display.empty() || r.display.channels() != 3 || !r.hasHist ||
            r.hist.size() != 256) {
            out << "SELFTEST_FAIL pipeline\n";
            return 1;
        }
    }
    // Rotate: exact quarters transpose the canvas, 0° is identity.
    {
        FilterParams rp;
        rp.angle = 90;
        const cv::Mat rot = applyFilter(frame, QStringLiteral("rotate"), rp);
        if (rot.rows != frame.cols || rot.cols != frame.rows) {
            out << "SELFTEST_FAIL rotate transpose\n";
            return 1;
        }
        FilterParams r0;
        const cv::Mat same = applyFilter(frame, QStringLiteral("rotate"), r0);
        if (same.rows != frame.rows || same.cols != frame.cols) {
            out << "SELFTEST_FAIL rotate identity\n";
            return 1;
        }
    }
    // Truncation: stage-0-only result must differ from the full chain.
    {
        const PipeResult a = applyPipeline(frame, pipe, 0, 0);
        const PipeResult b = applyPipeline(frame, pipe, -1, -1);
        if (cv::countNonZero(a.display.reshape(1) != b.display.reshape(1)) == 0) {
            out << "SELFTEST_FAIL truncation\n";
            return 1;
        }
    }

    // Benchmark: full 9-filter chain on 720p, timed.
    const int iters = 20;
    QElapsedTimer t;
    t.start();
    for (int i = 0; i < iters; ++i) {
        const PipeResult r = applyPipeline(frame, pipe, -1, -1);
        if (r.display.empty()) {
            out << "SELFTEST_FAIL empty\n";
            return 1;
        }
    }
    const double ms = t.elapsed() / static_cast<double>(iters);
    out << "SELFTEST_OK chain_ms=" << QString::number(ms, 'f', 2) << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLatin1(argv[i]) == QStringLiteral("--self-test")) {
            QCoreApplication app(argc, argv);
            Q_UNUSED(app);
            return selfTest();
        }
    }

    QApplication app(argc, argv);
    QFont font(QStringLiteral("JetBrainsMono Nerd Font"));
    if (!font.exactMatch()) font = QFont(QStringLiteral("monospace"), 10);
    app.setFont(font);

    const Palette pal = loadOmarchyPalette();
    QPalette qpal;
    qpal.setColor(QPalette::Window, QColor(pal.bg));
    qpal.setColor(QPalette::WindowText, QColor(pal.fg));
    qpal.setColor(QPalette::Base, QColor(pal.darkBg));
    qpal.setColor(QPalette::Text, QColor(pal.fg));
    qpal.setColor(QPalette::Button, QColor(pal.lighterBg));
    qpal.setColor(QPalette::ButtonText, QColor(pal.fg));
    app.setPalette(qpal);

    qRegisterMetaType<FilterParams>();
    qRegisterMetaType<FilterStage>();
    qRegisterMetaType<HistData>();

    MainWindow win;
    win.show();
    return app.exec();
}
