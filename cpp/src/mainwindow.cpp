#include "mainwindow.h"

#include <opencv2/imgproc.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include "version.h"
#include "worker.h"

namespace {

QImage matToQImage(const cv::Mat& frame) {
    if (frame.empty()) return QImage();
    cv::Mat rgb;
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step),
                  QImage::Format_RGB888)
        .copy();
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), pal_(loadOmarchyPalette()) {
    setWindowTitle(QStringLiteral("OPENCAM"));
    setMinimumSize(820, 520);
    resize(980, 620);

    cameras_ = listUsbCameras();
    if (!cameras_.empty()) {
        currentCam_ = cameras_[0].index;
        currentPath_ = cameras_[0].path;
    }

    // The stack always opens with a pinned "No Filter" stage at the top so
    // the raw frame is one click away. It cannot be removed or moved.
    FilterStage raw;
    raw.type = QStringLiteral("none");
    raw.name = QStringLiteral("No Filter");
    pipeline_.push_back(raw);
    selected_ = 0;

    buildUi();
    applyStyles();
    populateCameras();
    rebuildFilterList();
    panel_->setFilter(QStringLiteral("none"), FilterParams(), 0);

    camTimer_ = new QTimer(this);
    connect(camTimer_, &QTimer::timeout, this, &MainWindow::refreshCamerasIfNeeded);
    camTimer_->start(8000);
}

MainWindow::~MainWindow() { stopStream(); }

void MainWindow::closeEvent(QCloseEvent* event) {
    stopStream();
    QMainWindow::closeEvent(event);
}

void MainWindow::buildUi() {
    QWidget* central = new QWidget();
    setCentralWidget(central);
    QVBoxLayout* outerV = new QVBoxLayout(central);
    outerV->setContentsMargins(6, 6, 6, 6);
    outerV->setSpacing(4);

    QFrame* titleBar = new QFrame();
    titleBar->setStyleSheet(QStringLiteral("background: %1; border-bottom: 1px solid %2;")
                                .arg(pal_.lighterBg, pal_.border));
    QHBoxLayout* titleL = new QHBoxLayout(titleBar);
    titleL->setContentsMargins(12, 6, 12, 6);
    QLabel* title = new QLabel(QStringLiteral("OPENCAM"));
    title->setStyleSheet(
        QStringLiteral("font: bold 13pt monospace; color: %1; letter-spacing: 2px;")
            .arg(pal_.accent));
    titleL->addWidget(title);
    titleL->addStretch();
    outerV->addWidget(titleBar);

    QHBoxLayout* mainH = new QHBoxLayout();
    mainH->setSpacing(6);

    QFrame* sidebar = new QFrame();
    sidebar->setFixedWidth(235);
    sidebar->setFrameShape(QFrame::StyledPanel);
    QVBoxLayout* sideV = new QVBoxLayout(sidebar);
    sideV->setContentsMargins(8, 8, 8, 8);
    sideV->setSpacing(3);

    toggleBtn_ = new QPushButton(QStringLiteral("▶  START STREAM"));
    toggleBtn_->setMinimumHeight(30);
    connect(toggleBtn_, &QPushButton::clicked, this, &MainWindow::toggleStream);
    sideV->addWidget(toggleBtn_);
    sideV->addSpacing(6);

    sideV->addWidget(new QLabel(QStringLiteral("Camera")));
    camCombo_ = new QComboBox();
    camCombo_->setMinimumWidth(210);
    connect(camCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::onCamChanged);
    sideV->addWidget(camCombo_);

    pathLabel_ = new QLabel(QStringLiteral("Path: —"));
    pathLabel_->setStyleSheet(
        QStringLiteral("color: %1; font: 8pt monospace;").arg(pal_.muted));
    sideV->addWidget(pathLabel_);
    sideV->addSpacing(4);

    sideV->addWidget(new QLabel(QStringLiteral("Add Filter")));
    QHBoxLayout* addH = new QHBoxLayout();
    addCombo_ = new QComboBox();
    for (const FilterChoice& fc : availableFilters()) addCombo_->addItem(fc.label, fc.key);
    addCombo_->setMinimumWidth(150);
    addBtn_ = new QPushButton(QStringLiteral("Add"));
    compactButton(addBtn_);
    connect(addBtn_, &QPushButton::clicked, this, &MainWindow::addFilter);
    addH->addWidget(addCombo_, 1);
    addH->addWidget(addBtn_);
    sideV->addLayout(addH);
    sideV->addSpacing(4);

    sideV->addWidget(new QLabel(QStringLiteral("Applied Filters (numbered)")));
    filterList_ = new QListWidget();
    filterList_->setFixedHeight(90);
    connect(filterList_, &QListWidget::itemSelectionChanged, this,
            &MainWindow::onFilterListSelection);
    sideV->addWidget(filterList_);

    QHBoxLayout* btnsH = new QHBoxLayout();
    removeBtn_ = new QPushButton(QStringLiteral("✕"));
    upBtn_ = new QPushButton(QStringLiteral("↑"));
    downBtn_ = new QPushButton(QStringLiteral("↓"));
    for (QPushButton* b : {removeBtn_, upBtn_, downBtn_}) {
        b->setFixedWidth(34);
        compactButton(b);
    }
    connect(removeBtn_, &QPushButton::clicked, this, &MainWindow::removeSelectedFilter);
    connect(upBtn_, &QPushButton::clicked, this, &MainWindow::moveFilterUp);
    connect(downBtn_, &QPushButton::clicked, this, &MainWindow::moveFilterDown);
    btnsH->addWidget(removeBtn_);
    btnsH->addWidget(upBtn_);
    btnsH->addWidget(downBtn_);
    btnsH->addStretch();
    sideV->addLayout(btnsH);
    sideV->addStretch();

    statusLbl_ = new QLabel(QStringLiteral("Idle"));
    statusLbl_->setStyleSheet(
        QStringLiteral("color: %1; font: 8pt monospace;").arg(pal_.muted));
    sideV->addWidget(statusLbl_);
    mainH->addWidget(sidebar);

    QHBoxLayout* content = new QHBoxLayout();
    content->setSpacing(6);
    QVBoxLayout* vcontent = new QVBoxLayout();
    vcontent->setSpacing(2);
    pipelineLabel_ = new QLabel(QStringLiteral("Pipeline: (none)"));
    pipelineLabel_->setStyleSheet(
        QStringLiteral("color: %1; font: 8pt monospace; padding: 1px 4px; background: %2; border: 1px solid %3;")
            .arg(pal_.muted, pal_.darkBg, pal_.border));
    pipelineLabel_->setWordWrap(true);
    vcontent->addWidget(pipelineLabel_);
    sizeLabel_ = new QLabel(QStringLiteral("—"));
    sizeLabel_->setStyleSheet(
        QStringLiteral("color: %1; font: 8pt monospace; padding: 1px 4px;").arg(pal_.muted));
    sizeLabel_->setToolTip(
        QStringLiteral("Current image size (height × width) after the viewed stage"));
    vcontent->addWidget(sizeLabel_);
    video_ = new VideoDisplay(pal_);
    vcontent->addWidget(video_, 3);
    content->addLayout(vcontent, 3);

    panel_ = new FilterPanel(pal_);
    connect(panel_, &FilterPanel::paramsChanged, this, &MainWindow::onPanelParams);
    QScrollArea* scroll = new QScrollArea();
    scroll->setWidget(panel_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumWidth(230);
    content->addWidget(scroll, 1);
    mainH->addLayout(content, 1);
    outerV->addLayout(mainH, 1);

    QLabel* footer = new QLabel(
        QStringLiteral("Tip: Use Binary + drag histogram line while adjusting the C-mount "
                       "focus ring for sharp contrast.  •  opencam v%1")
            .arg(QString::fromLatin1(APP_VERSION)));
    footer->setStyleSheet(
        QStringLiteral("color: %1; font: 7pt monospace; padding: 1px 4px;").arg(pal_.muted));
    outerV->addWidget(footer);
}

void MainWindow::compactButton(QPushButton* btn) {
    btn->setStyleSheet(
        QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3;"
                       " border-radius: 4px; padding: 4px 8px; font: 10pt monospace; }"
                       "QPushButton:hover { border: 1px solid %4; background: #2a2b3a; }"
                       "QPushButton:pressed { background: %4; color: %5; }")
            .arg(pal_.lighterBg, pal_.fg, pal_.border, pal_.accent, pal_.bg));
}

void MainWindow::applyStyles() {
    setStyleSheet(
        QStringLiteral(
            "QMainWindow, QWidget { background: %1; color: %2; font-family: monospace; }"
            "QLabel { color: %2; }"
            "QPushButton { background: %3; color: %2; border: 1px solid %4;"
            " border-radius: 4px; padding: 6px 14px; font: 10pt monospace; }"
            "QPushButton:hover { border: 1px solid %5; background: #2a2b3a; }"
            "QPushButton:pressed { background: %5; color: %1; }"
            "QComboBox { background: %3; color: %2; border: 1px solid %4;"
            " border-radius: 4px; padding: 4px 8px; font: 10pt monospace; }"
            "QComboBox:focus { border: 1px solid %5; }"
            "QComboBox QAbstractItemView { background: %6; color: %2; border: 1px solid %4;"
            " selection-background-color: %5; selection-color: %1; outline: 0; }"
            "QComboBox QAbstractItemView::item { min-height: 22px; padding: 3px 10px; }"
            "QListWidget { background: %6; color: %2; border: 1px solid %4;"
            " border-radius: 4px; font: 9pt monospace; outline: 0; }"
            "QListWidget::item { padding: 3px 6px; }"
            "QListWidget::item:hover { background: %3; }"
            "QListWidget::item:selected { background: %5; color: %1; }")
            .arg(pal_.bg, pal_.fg, pal_.lighterBg, pal_.border, pal_.accent, pal_.darkBg));
    updateToggleStyle();
}

void MainWindow::updateToggleStyle() {
    if (streaming_) {
        toggleBtn_->setText(QStringLiteral("⏹  STOP STREAM"));
        toggleBtn_->setStyleSheet(
            QStringLiteral("QPushButton { background: #3a2a2a; color: %1; border: 1px solid %1;"
                           " border-radius: 4px; padding: 6px 14px; font: bold 10pt monospace; }"
                           "QPushButton:hover { background: #4a2f2f; }")
                .arg(pal_.redLine));
    } else {
        toggleBtn_->setText(QStringLiteral("▶  START STREAM"));
        toggleBtn_->setStyleSheet(
            QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3;"
                           " border-radius: 4px; padding: 6px 14px; font: 10pt monospace; }"
                           "QPushButton:hover { background: #2a3a2a; }")
                .arg(pal_.lighterBg, pal_.accentHover, pal_.accent));
    }
}

void MainWindow::populateCameras() {
    camCombo_->blockSignals(true);
    camCombo_->clear();
    if (cameras_.empty()) {
        camCombo_->addItem(QStringLiteral("No cameras found"));
        statusLbl_->setText(QStringLiteral("No V4L2 cameras detected"));
        pathLabel_->setText(QStringLiteral("Path: —"));
        currentCam_ = -1;
    } else {
        for (const CamInfo& c : cameras_)
            camCombo_->addItem(QStringLiteral("%1   (%2)").arg(c.label, c.path));
        camCombo_->setCurrentIndex(0);
        currentCam_ = cameras_[0].index;
        currentPath_ = cameras_[0].path;
        pathLabel_->setText(QStringLiteral("Path: %1").arg(currentPath_));
    }
    camCombo_->blockSignals(false);
}

void MainWindow::refreshCamerasIfNeeded() {
    if (streaming_) return;
    const std::vector<CamInfo> fresh = listUsbCameras();
    bool same = fresh.size() == cameras_.size();
    if (same) {
        for (size_t i = 0; i < fresh.size(); ++i) {
            if (fresh[i].index != cameras_[i].index || fresh[i].label != cameras_[i].label) {
                same = false;
                break;
            }
        }
    }
    if (!same) {
        cameras_ = fresh;
        populateCameras();
    }
}

void MainWindow::onCamChanged(int idx) {
    if (idx < 0 || idx >= static_cast<int>(cameras_.size())) return;
    const int newIdx = cameras_[idx].index;
    const QString newPath = cameras_[idx].path;
    if (streaming_) {
        if (newIdx != currentCam_) {
            currentCam_ = newIdx;
            currentPath_ = newPath;
            pathLabel_->setText(QStringLiteral("Path: %1").arg(newPath));
            restartStream();
        }
    } else {
        currentCam_ = newIdx;
        currentPath_ = newPath;
        pathLabel_->setText(QStringLiteral("Path: %1").arg(newPath));
    }
}

void MainWindow::onPanelParams(int idx, FilterParams params) {
    if (idx >= 0 && idx < static_cast<int>(pipeline_.size())) {
        pipeline_[idx].params = params;
        updateWorker();
    }
}

void MainWindow::updatePipelineLabel() {
    if (pipeline_.empty()) {
        pipelineLabel_->setText(QStringLiteral("Pipeline: (none)"));
        return;
    }
    // The raw base is structural, not a choice: unnumbered, always first.
    // Real filters are numbered 1..N over non-none stages.
    QStringList parts;
    int n = 0;
    for (size_t i = 0; i < pipeline_.size(); ++i) {
        QString part;
        if (pipeline_[i].type == QStringLiteral("none")) {
            part = QStringLiteral("Raw");
        } else {
            part = QStringLiteral("%1:%2").arg(++n).arg(
                pipeline_[i].name.split(' ').value(0).left(12));
        }
        if (static_cast<int>(i) == selected_) part = QStringLiteral("[%1]").arg(part);
        parts << part;
    }
    pipelineLabel_->setText(parts.join(QStringLiteral(" → ")));
}

void MainWindow::rebuildFilterList() {
    filterList_->blockSignals(true);
    filterList_->clear();
    int n = 0;
    for (size_t i = 0; i < pipeline_.size(); ++i) {
        QListWidgetItem* item = nullptr;
        if (pipeline_[i].type == QStringLiteral("none")) {
            item = new QListWidgetItem(QStringLiteral("No Filter \u2014 raw base"));
        } else {
            item = new QListWidgetItem(
                QStringLiteral("%1. %2").arg(++n).arg(pipeline_[i].name));
        }
        item->setData(Qt::ItemDataRole::UserRole, static_cast<int>(i));
        filterList_->addItem(item);
    }
    if (selected_ >= 0 && selected_ < static_cast<int>(pipeline_.size()))
        filterList_->setCurrentRow(selected_);
    filterList_->blockSignals(false);
    updatePipelineLabel();
    updateListButtons();
}

void MainWindow::updateListButtons() {
    // The raw base (idx 0) is structural: nothing may touch it, and no real
    // filter may move above it (up needs idx > 1).
    removeBtn_->setEnabled(selected_ > 0);
    upBtn_->setEnabled(selected_ > 1);
    downBtn_->setEnabled(selected_ > 0);
}

void MainWindow::addFilter() {
    const int ci = addCombo_->currentIndex();
    if (ci < 0 || ci >= static_cast<int>(availableFilters().size())) return;
    const FilterChoice& fc = availableFilters()[ci];
    FilterStage stage;
    stage.type = fc.key;
    stage.name = fc.label;
    stage.params = defaultParams(fc.key);
    pipeline_.push_back(stage);
    selected_ = static_cast<int>(pipeline_.size()) - 1;
    rebuildFilterList();
    panel_->setFilter(stage.type, stage.params, selected_);
    updateWorker();
}

void MainWindow::onFilterListSelection() {
    const QList<QListWidgetItem*> items = filterList_->selectedItems();
    if (items.isEmpty()) {
        selected_ = -1;
    } else {
        bool ok = false;
        const int idx = items[0]->data(Qt::ItemDataRole::UserRole).toInt(&ok);
        if (!ok || idx < 0 || idx >= static_cast<int>(pipeline_.size())) return;
        selected_ = idx;
    }
    QTimer::singleShot(0, this, [this] { applySelection(selected_); });
}

void MainWindow::applySelection(int idx) {
    if (idx != selected_) return;  // superseded by a newer selection
    if (idx >= 0 && idx < static_cast<int>(pipeline_.size())) {
        const FilterStage& f = pipeline_[idx];
        panel_->setFilter(f.type, f.params, idx);
    } else {
        panel_->setFilter(QStringLiteral("none"), FilterParams(), -1);
    }
    updatePipelineLabel();
    updateListButtons();
    updateWorker();
}

void MainWindow::removeSelectedFilter() {
    if (selected_ <= 0 || selected_ >= static_cast<int>(pipeline_.size())) return;  // pinned
    pipeline_.erase(pipeline_.begin() + selected_);
    selected_ = pipeline_.empty() ? -1 : std::min(selected_, static_cast<int>(pipeline_.size()) - 1);
    rebuildFilterList();
    if (selected_ >= 0) {
        const FilterStage& f = pipeline_[selected_];
        panel_->setFilter(f.type, f.params, selected_);
    } else {
        panel_->setFilter(QStringLiteral("none"), FilterParams(), -1);
    }
    updateWorker();
}

void MainWindow::moveFilterUp() {
    if (selected_ <= 1 || selected_ >= static_cast<int>(pipeline_.size())) return;  // never above raw base
    std::swap(pipeline_[selected_ - 1], pipeline_[selected_]);
    --selected_;
    rebuildFilterList();
    const FilterStage& f = pipeline_[selected_];
    panel_->setFilter(f.type, f.params, selected_);
    updateWorker();
}

void MainWindow::moveFilterDown() {
    // Stage 1 (No Filter) stays pinned at the top.
    if (selected_ <= 0 || selected_ >= static_cast<int>(pipeline_.size()) - 1) return;
    std::swap(pipeline_[selected_], pipeline_[selected_ + 1]);
    ++selected_;
    rebuildFilterList();
    const FilterStage& f = pipeline_[selected_];
    panel_->setFilter(f.type, f.params, selected_);
    updateWorker();
}

void MainWindow::updateWorker() {
    if (streaming_ && worker_) {
        worker_->updateSettings(currentCam_, pipeline_, selected_);
    } else if (!streaming_) {
        renderStill();
    }
}

void MainWindow::renderStill() {
    if (lastRaw_.empty()) return;
    const int upto =
        (selected_ >= 0 && selected_ < static_cast<int>(pipeline_.size())) ? selected_ : -1;
    const PipeResult r = applyPipeline(lastRaw_, pipeline_, selected_, upto);
    if (r.display.empty()) return;
    showFrame(matToQImage(r.display));
    if (r.hasHist && selected_ >= 0 && selected_ < static_cast<int>(pipeline_.size()) &&
        pipeline_[selected_].type == QStringLiteral("binary")) {
        panel_->setHist(QVector<int>(r.hist.begin(), r.hist.end()), r.thresh);
    }
}

void MainWindow::toggleStream() {
    if (streaming_) stopStream();
    else startStream();
}

void MainWindow::startStream() {
    if (cameras_.empty() || currentCam_ < 0) {
        QMessageBox::warning(this, QStringLiteral("opencam"),
                             QStringLiteral("No camera selected."));
        return;
    }
    streaming_ = true;
    updateToggleStyle();
    statusLbl_->setText(QStringLiteral("Streaming..."));

    worker_ = new CameraWorker(currentCam_, pipeline_, selected_);
    thread_ = new QThread(this);
    worker_->moveToThread(thread_);
    connect(thread_, &QThread::started, worker_, &CameraWorker::run);
    connect(worker_, &CameraWorker::frameReady, this, &MainWindow::onFrame);
    connect(worker_, &CameraWorker::fpsReady, this, &MainWindow::onFps);
    connect(worker_, &CameraWorker::error, this, &MainWindow::onError);
    connect(worker_, &CameraWorker::finished, this, &MainWindow::onWorkerFinished);
    thread_->start();
    worker_->updateSettings(currentCam_, pipeline_, selected_);
}

void MainWindow::restartStream() {
    stopStream();
    QTimer::singleShot(60, this, &MainWindow::startStream);
}

void MainWindow::stopStream() {
    streaming_ = false;
    updateToggleStyle();
    if (worker_) worker_->stop();
    if (thread_) {
        thread_->quit();
        thread_->wait(1500);
        thread_->deleteLater();
        thread_ = nullptr;
    }
    if (worker_) {
        const cv::Mat raw = worker_->lastRaw();  // safe: run() exited after wait()
        if (!raw.empty()) lastRaw_ = raw;
        worker_->deleteLater();
        worker_ = nullptr;
    }
    if (!lastRaw_.empty()) {
        renderStill();
        statusLbl_->setText(QStringLiteral("Stopped — still frame (filters editable)"));
    } else {
        showFrame(QImage());
        statusLbl_->setText(QStringLiteral("Stopped"));
    }
}

void MainWindow::showFrame(const QImage& img) {
    video_->setFrame(img);
    if (img.isNull()) {
        sizeLabel_->setText(QStringLiteral("—"));
    } else {
        sizeLabel_->setText(QStringLiteral("%1 × %2").arg(img.height()).arg(img.width()));
    }
}

void MainWindow::onFrame(const QImage& img, HistData hist) {
    showFrame(img);
    if (hist.valid && selected_ >= 0 && selected_ < static_cast<int>(pipeline_.size()) &&
        pipeline_[selected_].type == QStringLiteral("binary")) {
        panel_->setHist(hist.bins, hist.thresh);
    }
}

void MainWindow::onFps(double fps) {
    if (streaming_)
        statusLbl_->setText(QStringLiteral("%1 fps  •  %2").arg(fps, 0, 'f', 1).arg(camLabel()));
}

QString MainWindow::camLabel() const {
    for (const CamInfo& c : cameras_) {
        if (c.index == currentCam_) {
            QString lbl = c.label;
            const int colon = lbl.indexOf(':');
            if (colon >= 0) lbl = lbl.mid(colon + 1).trimmed();
            return lbl.left(22);
        }
    }
    return QString::number(currentCam_);
}

void MainWindow::onError(const QString& msg) {
    stopStream();
    statusLbl_->setText(QStringLiteral("Error: %1").arg(msg));
    QMessageBox::critical(this, QStringLiteral("opencam"), msg);
}

void MainWindow::onWorkerFinished() {}
