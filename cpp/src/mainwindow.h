#pragma once

// Main window: sidebar + live view + dynamic filter panel (mirrors the
// Python MainWindow, including still-frame editing and cumulative preview).

#include <opencv2/core.hpp>

#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QThread>
#include <QTimer>

#include "cameras.h"
#include "filterpanel.h"
#include "filters.h"
#include "theme.h"
#include "videodisplay.h"
#include "worker.h"

class QComboBox;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onCamChanged(int idx);
    void onPanelParams(int idx, FilterParams params);
    void onFrame(const QImage& img, HistData hist);
    void onFps(double fps);
    void onError(const QString& msg);
    void onWorkerFinished();

private:
    void buildUi();
    void applyStyles();
    void compactButton(QPushButton* btn);
    void populateCameras();
    void refreshCamerasIfNeeded();
    void addFilter();
    void onFilterListSelection();
    void applySelection(int idx);
    void removeSelectedFilter();
    void moveFilterUp();
    void moveFilterDown();
    void updatePipelineLabel();
    void rebuildFilterList();
    void updateListButtons();
    void updateWorker();
    void renderStill();
    void showFrame(const QImage& img);
    void toggleStream();
    void startStream();
    void restartStream();
    void stopStream();
    QString camLabel() const;
    void updateToggleStyle();

    Palette pal_;
    std::vector<CamInfo> cameras_;
    int currentCam_ = -1;
    QString currentPath_;
    std::vector<FilterStage> pipeline_;
    int selected_ = -1;
    bool streaming_ = false;
    cv::Mat lastRaw_;
    CameraWorker* worker_ = nullptr;
    QThread* thread_ = nullptr;

    QPushButton* toggleBtn_ = nullptr;
    QComboBox* camCombo_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QComboBox* addCombo_ = nullptr;
    QPushButton* addBtn_ = nullptr;
    QListWidget* filterList_ = nullptr;
    QPushButton* removeBtn_ = nullptr;
    QPushButton* upBtn_ = nullptr;
    QPushButton* downBtn_ = nullptr;
    QLabel* statusLbl_ = nullptr;
    QLabel* pipelineLabel_ = nullptr;
    QLabel* sizeLabel_ = nullptr;
    VideoDisplay* video_ = nullptr;
    FilterPanel* panel_ = nullptr;
    QTimer* camTimer_ = nullptr;
};
