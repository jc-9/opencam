#pragma once

// Right-side dynamic parameter panel: one stacked caption+control group per
// filter parameter (mirrors the Python FilterPanel).

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <initializer_list>

#include "filters.h"
#include "histogram.h"
#include "theme.h"

class FilterPanel : public QWidget {
    Q_OBJECT
public:
    explicit FilterPanel(const Palette& pal, QWidget* parent = nullptr);

    void setFilter(const QString& key, const FilterParams& params, int idx);
    FilterParams currentParams() const;
    void setHist(const QVector<int>& bins, int thresh);

signals:
    void paramsChanged(int idx, FilterParams params);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void clearLayout();
    void emitParams();
    void syncFromHist(int thresh);
    QLabel* hint(const QString& text);
    QLabel* titleLabel(const QString& text);
    QLabel* subLabel(const QString& text);
    // Caption above a row of controls (keeps the panel narrow).
    void stacked(const QString& caption, std::initializer_list<QWidget*> controls);
    QSpinBox* makeSpin(int lo, int hi, int val);
    QDoubleSpinBox* makeDSpin(double lo, double hi, double val, double step);
    QComboBox* makeCombo();
    QString spinStyle() const;
    QString comboStyle() const;

    void buildNone();
    void buildBinary(const FilterParams& p);
    void buildGaussian(const FilterParams& p);
    void buildSobel(const FilterParams& p);
    void buildColor(const FilterParams& p);
    void buildMorph(const FilterParams& p);
    void buildCanny(const FilterParams& p);
    void buildMedian(const FilterParams& p);
    void buildBilateral(const FilterParams& p);
    void buildRotate(const FilterParams& p);
    void buildColorThreshold(const FilterParams& p);
    void updateColorThresholdLabels();

    Palette pal_;
    QVBoxLayout* layout_ = nullptr;
    QString currentKey_ = QStringLiteral("none");
    int currentIdx_ = -1;

    // Recreated on every build; null when the current filter lacks them.
    QSpinBox* threshSpin_ = nullptr;
    HistogramWidget* hist_ = nullptr;
    QComboBox* kernelCombo_ = nullptr;
    QSpinBox* passesSpin_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QComboBox* operationCombo_ = nullptr;
    QComboBox* kernelShapeCombo_ = nullptr;
    QSpinBox* kernelSizeSpin_ = nullptr;
    QSpinBox* iterationsSpin_ = nullptr;
    QSpinBox* thresh1Spin_ = nullptr;
    QSpinBox* thresh2Spin_ = nullptr;
    QComboBox* apertureCombo_ = nullptr;
    QCheckBox* l2Check_ = nullptr;
    QSpinBox* dSpin_ = nullptr;
    QDoubleSpinBox* sigmaColorSpin_ = nullptr;
    QDoubleSpinBox* sigmaSpaceSpin_ = nullptr;
    QComboBox* colorSpaceCombo_ = nullptr;
    QSpinBox* lowerHSpin_ = nullptr;
    QSpinBox* lowerSSpin_ = nullptr;
    QSpinBox* lowerVSpin_ = nullptr;
    QSpinBox* upperHSpin_ = nullptr;
    QSpinBox* upperSSpin_ = nullptr;
    QSpinBox* upperVSpin_ = nullptr;
    QSpinBox* angleSpin_ = nullptr;
    std::vector<QLabel*> lowerSubs_;
    std::vector<QLabel*> upperSubs_;
    QCheckBox* invertCheck_ = nullptr;
};
