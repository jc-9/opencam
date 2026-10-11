#include "filterpanel.h"

#include <QResizeEvent>
#include <QTimer>

#include <algorithm>
#include <utility>
#include <vector>

FilterPanel::FilterPanel(const Palette& pal, QWidget* parent)
    : QWidget(parent), pal_(pal) {
    setMinimumWidth(0);
    setMaximumWidth(280);
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(8, 8, 8, 8);
    layout_->setSpacing(6);
    buildNone();
}

void FilterPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (layout_) layout_->activate();
}

void FilterPanel::clearLayout() {
    threshSpin_ = nullptr;
    hist_ = nullptr;
    kernelCombo_ = nullptr;
    passesSpin_ = nullptr;
    modeCombo_ = nullptr;
    operationCombo_ = nullptr;
    kernelShapeCombo_ = nullptr;
    kernelSizeSpin_ = nullptr;
    iterationsSpin_ = nullptr;
    thresh1Spin_ = nullptr;
    thresh2Spin_ = nullptr;
    apertureCombo_ = nullptr;
    l2Check_ = nullptr;
    dSpin_ = nullptr;
    sigmaColorSpin_ = nullptr;
    sigmaSpaceSpin_ = nullptr;
    colorSpaceCombo_ = nullptr;
    lowerHSpin_ = lowerSSpin_ = lowerVSpin_ = nullptr;
    upperHSpin_ = upperSSpin_ = upperVSpin_ = nullptr;
    angleSpin_ = nullptr;
    lowerSubs_.clear();
    upperSubs_.clear();
    invertCheck_ = nullptr;
    QLayoutItem* item = nullptr;
    while ((item = layout_->takeAt(0)) != nullptr) {
        if (item->layout()) {
            QLayoutItem* sub = nullptr;
            while ((sub = item->layout()->takeAt(0)) != nullptr) {
                if (sub->widget()) sub->widget()->deleteLater();
                delete sub;
            }
            delete item->layout();
        }
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

void FilterPanel::setFilter(const QString& key, const FilterParams& params, int idx) {
    currentKey_ = key;
    currentIdx_ = idx;
    clearLayout();
    if (key == QStringLiteral("binary")) buildBinary(params);
    else if (key == QStringLiteral("gaussian")) buildGaussian(params);
    else if (key == QStringLiteral("sobelx")) buildSobel(params);
    else if (key == QStringLiteral("color")) buildColor(params);
    else if (key == QStringLiteral("morph")) buildMorph(params);
    else if (key == QStringLiteral("canny")) buildCanny(params);
    else if (key == QStringLiteral("median")) buildMedian(params);
    else if (key == QStringLiteral("bilateral")) buildBilateral(params);
    else if (key == QStringLiteral("color_threshold")) buildColorThreshold(params);
    else if (key == QStringLiteral("rotate")) buildRotate(params);
    else buildNone();
    layout_->activate();
    const int emitIdx = currentIdx_;
    QTimer::singleShot(20, this, [this, emitIdx] { emit paramsChanged(emitIdx, currentParams()); });
}

void FilterPanel::emitParams() { emit paramsChanged(currentIdx_, currentParams()); }

void FilterPanel::syncFromHist(int t) {
    if (threshSpin_) {
        threshSpin_->blockSignals(true);
        threshSpin_->setValue(t);
        threshSpin_->blockSignals(false);
    }
    emitParams();
}

void FilterPanel::setHist(const QVector<int>& bins, int thresh) {
    if (hist_) hist_->setData(bins, thresh);
}

FilterParams FilterPanel::currentParams() const {
    FilterParams p;
    if (currentKey_ == QStringLiteral("binary")) {
        if (threshSpin_) p.thresh = threshSpin_->value();
    } else if (currentKey_ == QStringLiteral("gaussian")) {
        if (kernelCombo_) p.kernel = kernelCombo_->currentData().toInt();
        if (passesSpin_) p.passes = passesSpin_->value();
    } else if (currentKey_ == QStringLiteral("sobelx")) {
        if (kernelCombo_) p.kernel = kernelCombo_->currentData().toInt();
    } else if (currentKey_ == QStringLiteral("color")) {
        if (modeCombo_) p.mode = modeCombo_->currentData().toString();
    } else if (currentKey_ == QStringLiteral("morph")) {
        if (operationCombo_) p.operation = operationCombo_->currentData().toString();
        if (kernelShapeCombo_) p.kernelShape = kernelShapeCombo_->currentData().toString();
        if (kernelSizeSpin_) p.kernelSize = kernelSizeSpin_->value();
        if (iterationsSpin_) p.iterations = iterationsSpin_->value();
    } else if (currentKey_ == QStringLiteral("canny")) {
        if (thresh1Spin_) p.threshold1 = thresh1Spin_->value();
        if (thresh2Spin_) p.threshold2 = thresh2Spin_->value();
        if (apertureCombo_) p.aperture = apertureCombo_->currentData().toInt();
        if (l2Check_) p.l2 = l2Check_->isChecked();
    } else if (currentKey_ == QStringLiteral("median")) {
        if (kernelSizeSpin_) p.kernelSize = kernelSizeSpin_->value();
    } else if (currentKey_ == QStringLiteral("bilateral")) {
        if (dSpin_) p.d = dSpin_->value();
        if (sigmaColorSpin_) p.sigmaColor = sigmaColorSpin_->value();
        if (sigmaSpaceSpin_) p.sigmaSpace = sigmaSpaceSpin_->value();
    } else if (currentKey_ == QStringLiteral("color_threshold")) {
        if (colorSpaceCombo_) p.colorSpace = colorSpaceCombo_->currentData().toString();
        if (lowerHSpin_) p.lowerH = lowerHSpin_->value();
        if (lowerSSpin_) p.lowerS = lowerSSpin_->value();
        if (lowerVSpin_) p.lowerV = lowerVSpin_->value();
        if (upperHSpin_) p.upperH = upperHSpin_->value();
        if (upperSSpin_) p.upperS = upperSSpin_->value();
        if (upperVSpin_) p.upperV = upperVSpin_->value();
        if (invertCheck_) p.invert = invertCheck_->isChecked();
    } else if (currentKey_ == QStringLiteral("rotate")) {
        if (angleSpin_) p.angle = angleSpin_->value();
    }
    return p;
}

QString FilterPanel::spinStyle() const {
    return QStringLiteral(
               "QSpinBox, QDoubleSpinBox { background: %1; color: %2; border: 1px solid %3;"
               " border-radius: 3px; padding: 2px 6px; font: 10pt monospace; }"
               "QSpinBox:focus, QDoubleSpinBox:focus { border: 1px solid %4; }")
        .arg(pal_.lighterBg, pal_.fg, pal_.border, pal_.accent);
}

QString FilterPanel::comboStyle() const {
    return QStringLiteral(
               "QComboBox { background: %1; color: %2; border: 1px solid %3;"
               " border-radius: 3px; padding: 2px 6px; font: 10pt monospace; }"
               "QComboBox:focus { border: 1px solid %4; }")
        .arg(pal_.lighterBg, pal_.fg, pal_.border, pal_.accent);
}

QLabel* FilterPanel::hint(const QString& text) {
    QLabel* lbl = new QLabel(text);
    lbl->setStyleSheet(
        QStringLiteral("color: %1; font: 8pt monospace;").arg(pal_.muted));
    lbl->setWordWrap(true);
    return lbl;
}

void FilterPanel::stacked(const QString& caption, std::initializer_list<QWidget*> controls) {
    QLabel* lbl = new QLabel(caption);
    lbl->setStyleSheet(
        QStringLiteral("color: %1; font: 8pt monospace; padding: 1px 2px;").arg(pal_.muted));
    layout_->addWidget(lbl);
    QHBoxLayout* row = new QHBoxLayout();
    row->setSpacing(4);
    for (QWidget* w : controls) row->addWidget(w);
    layout_->addLayout(row);
}

QLabel* FilterPanel::subLabel(const QString& text) {
    QLabel* lbl = new QLabel(text);
    lbl->setStyleSheet(QStringLiteral("color: %1; font: 9pt monospace;").arg(pal_.fg));
    return lbl;
}

QSpinBox* FilterPanel::makeSpin(int lo, int hi, int val) {
    QSpinBox* s = new QSpinBox();
    s->setRange(lo, hi);
    s->setValue(val);
    s->setStyleSheet(spinStyle());
    connect(s, &QSpinBox::valueChanged, this, [this](int) { emitParams(); });
    return s;
}

QDoubleSpinBox* FilterPanel::makeDSpin(double lo, double hi, double val, double step) {
    QDoubleSpinBox* s = new QDoubleSpinBox();
    s->setRange(lo, hi);
    s->setValue(val);
    s->setSingleStep(step);
    s->setStyleSheet(spinStyle());
    connect(s, &QDoubleSpinBox::valueChanged, this, [this](double) { emitParams(); });
    return s;
}

QComboBox* FilterPanel::makeCombo() {
    QComboBox* c = new QComboBox();
    c->setStyleSheet(comboStyle());
    connect(c, &QComboBox::currentIndexChanged, this, [this](int) { emitParams(); });
    return c;
}

QLabel* FilterPanel::titleLabel(const QString& text) {
    QLabel* title = new QLabel(text);
    title->setStyleSheet(
        QStringLiteral("color: %1; font: bold 10pt monospace; letter-spacing: 1px;")
            .arg(pal_.accent));
    return title;
}

void FilterPanel::buildNone() {
    QLabel* lbl = new QLabel(QStringLiteral("No extra controls for this filter.\nRaw camera view."));
    lbl->setStyleSheet(QStringLiteral("color: %1; font: 10pt monospace;").arg(pal_.muted));
    lbl->setWordWrap(true);
    layout_->addWidget(lbl);
    layout_->addStretch();
}

void FilterPanel::buildBinary(const FilterParams& p) {
    layout_->addWidget(titleLabel(QStringLiteral("BINARY THRESHOLD")));
    threshSpin_ = makeSpin(0, 255, p.thresh);
    stacked(QStringLiteral("Threshold"), {threshSpin_});
    hist_ = new HistogramWidget(pal_);
    connect(hist_, &HistogramWidget::thresholdChanged, this, &FilterPanel::syncFromHist);
    connect(threshSpin_, &QSpinBox::valueChanged, hist_, &HistogramWidget::setThreshold);
    layout_->addWidget(hist_);
    layout_->addWidget(hint(QStringLiteral(
        "Drag the red line or type value.\nHelps tune C-mount focus contrast.")));
    layout_->addStretch();
}

void FilterPanel::buildGaussian(const FilterParams& p) {
    layout_->addWidget(titleLabel(QStringLiteral("GAUSSIAN SMOOTHING")));
    kernelCombo_ = makeCombo();
    for (int k : {3, 5, 7, 9}) kernelCombo_->addItem(QStringLiteral("%1×%1").arg(k), k);
    const std::vector<int> ks = {3, 5, 7, 9};
    auto it = std::find(ks.begin(), ks.end(), p.kernel);
    kernelCombo_->setCurrentIndex(it == ks.end() ? 1 : static_cast<int>(it - ks.begin()));
    stacked(QStringLiteral("Kernel Size"), {kernelCombo_});
    passesSpin_ = makeSpin(1, 5, p.passes);
    stacked(QStringLiteral("Passes"), {passesSpin_});
    layout_->addWidget(hint(QStringLiteral(
        "Applies Gaussian blur multiple times.\nHigher passes = stronger smoothing for lens setup.")));
    layout_->addStretch();
}

void FilterPanel::buildSobel(const FilterParams& p) {
    layout_->addWidget(titleLabel(QStringLiteral("SOBEL X (VERTICAL EDGES)")));
    kernelCombo_ = makeCombo();
    for (int k : {3, 5, 7}) kernelCombo_->addItem(QStringLiteral("%1×%1").arg(k), k);
    const std::vector<int> ks = {3, 5, 7};
    auto it = std::find(ks.begin(), ks.end(), p.kernel);
    kernelCombo_->setCurrentIndex(it == ks.end() ? 0 : static_cast<int>(it - ks.begin()));
    stacked(QStringLiteral("Kernel"), {kernelCombo_});
    layout_->addWidget(hint(QStringLiteral(
        "Detects horizontal intensity changes.\nUseful for checking lens alignment\nand fine focus on edges.")));
    layout_->addStretch();
}

void FilterPanel::buildColor(const FilterParams& p) {
    layout_->addWidget(titleLabel(QStringLiteral("IMAGE COLOR")));
    modeCombo_ = makeCombo();
    modeCombo_->addItem(QStringLiteral("Color"), QStringLiteral("color"));
    modeCombo_->addItem(QStringLiteral("Greyscale"), QStringLiteral("gray"));
    modeCombo_->setCurrentIndex(p.mode == QStringLiteral("gray") ? 1 : 0);
    stacked(QStringLiteral("Mode"), {modeCombo_});
    layout_->addWidget(hint(QStringLiteral(
        "Switch between color and greyscale view.\n'Color' uses original if available.")));
    layout_->addStretch();
}

void FilterPanel::buildMorph(const FilterParams& p) {
    layout_->addWidget(titleLabel(QStringLiteral("MORPHOLOGICAL")));
    operationCombo_ = makeCombo();
    const std::vector<QString> ops = {QStringLiteral("ERODE"), QStringLiteral("DILATE"),
                                      QStringLiteral("OPEN"), QStringLiteral("CLOSE"),
                                      QStringLiteral("GRADIENT"), QStringLiteral("TOPHAT"),
                                      QStringLiteral("BLACKHAT")};
    for (const QString& op : ops) operationCombo_->addItem(op, op);
    auto oit = std::find(ops.begin(), ops.end(), p.operation);
    operationCombo_->setCurrentIndex(oit == ops.end() ? 2 : static_cast<int>(oit - ops.begin()));
    stacked(QStringLiteral("Operation"), {operationCombo_});
    kernelShapeCombo_ = makeCombo();
    const std::vector<QString> shapes = {QStringLiteral("RECT"), QStringLiteral("ELLIPSE"),
                                         QStringLiteral("CROSS")};
    for (const QString& s : shapes) kernelShapeCombo_->addItem(s, s);
    auto sit = std::find(shapes.begin(), shapes.end(), p.kernelShape);
    kernelShapeCombo_->setCurrentIndex(sit == shapes.end() ? 0 : static_cast<int>(sit - shapes.begin()));
    stacked(QStringLiteral("Kernel Shape"), {kernelShapeCombo_});
    kernelSizeSpin_ = makeSpin(3, 21, p.kernelSize);
    kernelSizeSpin_->setSingleStep(2);
    stacked(QStringLiteral("Kernel Size (odd)"), {kernelSizeSpin_});
    iterationsSpin_ = makeSpin(1, 10, p.iterations);
    stacked(QStringLiteral("Iterations"), {iterationsSpin_});
    layout_->addWidget(hint(QStringLiteral("Morph ops for noise removal, edge detection etc.")));
    layout_->addStretch();
}

void FilterPanel::buildCanny(const FilterParams& p) {
    layout_->addWidget(titleLabel(QStringLiteral("CANNY EDGE")));
    thresh1Spin_ = makeSpin(0, 255, p.threshold1);
    stacked(QStringLiteral("Thresh 1"), {thresh1Spin_});
    thresh2Spin_ = makeSpin(0, 255, p.threshold2);
    stacked(QStringLiteral("Thresh 2"), {thresh2Spin_});
    apertureCombo_ = makeCombo();
    const std::vector<int> aps = {3, 5, 7};
    for (int k : aps) apertureCombo_->addItem(QString::number(k), k);
    auto it = std::find(aps.begin(), aps.end(), p.aperture);
    apertureCombo_->setCurrentIndex(it == aps.end() ? 0 : static_cast<int>(it - aps.begin()));
    stacked(QStringLiteral("Aperture"), {apertureCombo_});
    l2Check_ = new QCheckBox(QStringLiteral("L2 Gradient"));
    l2Check_->setChecked(p.l2);
    connect(l2Check_, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState) { emitParams(); });
    layout_->addWidget(l2Check_);
    layout_->addWidget(hint(QStringLiteral("Edge detection. Adjust thresholds for best edges.")));
    layout_->addStretch();
}

void FilterPanel::buildMedian(const FilterParams& p) {
    layout_->addWidget(titleLabel(QStringLiteral("MEDIAN BLUR")));
    kernelSizeSpin_ = makeSpin(3, 15, p.kernelSize);
    kernelSizeSpin_->setSingleStep(2);
    stacked(QStringLiteral("Kernel Size (odd)"), {kernelSizeSpin_});
    layout_->addWidget(hint(QStringLiteral("Non-linear noise reduction. Good for salt/pepper.")));
    layout_->addStretch();
}

void FilterPanel::buildBilateral(const FilterParams& p) {
    layout_->addWidget(titleLabel(QStringLiteral("BILATERAL FILTER")));
    dSpin_ = makeSpin(3, 15, p.d);
    stacked(QStringLiteral("Diameter (d)"), {dSpin_});
    sigmaColorSpin_ = makeDSpin(10, 150, p.sigmaColor, 5);
    stacked(QStringLiteral("Sigma Color"), {sigmaColorSpin_});
    sigmaSpaceSpin_ = makeDSpin(10, 150, p.sigmaSpace, 5);
    stacked(QStringLiteral("Sigma Space"), {sigmaSpaceSpin_});
    layout_->addWidget(hint(QStringLiteral("Edge-preserving smooth. Higher sigma = stronger effect.")));
    layout_->addStretch();
}

void FilterPanel::buildColorThreshold(const FilterParams& p) {
    layout_->addWidget(titleLabel(QStringLiteral("COLOR THRESHOLD (InRange)")));
    colorSpaceCombo_ = makeCombo();
    const std::vector<QString> css = {QStringLiteral("HSV"), QStringLiteral("LAB"),
                                      QStringLiteral("YCrCb")};
    for (const QString& cs : css) colorSpaceCombo_->addItem(cs, cs);
    auto it = std::find(css.begin(), css.end(), p.colorSpace);
    colorSpaceCombo_->setCurrentIndex(it == css.end() ? 0 : static_cast<int>(it - css.begin()));
    disconnect(colorSpaceCombo_, nullptr, nullptr, nullptr);
    connect(colorSpaceCombo_, &QComboBox::currentIndexChanged, this,
            &FilterPanel::updateColorThresholdLabels);
    connect(colorSpaceCombo_, &QComboBox::currentIndexChanged, this, [this](int) { emitParams(); });
    stacked(QStringLiteral("Color Space"), {colorSpaceCombo_});

    lowerHSpin_ = makeSpin(0, 180, p.lowerH);
    lowerSSpin_ = makeSpin(0, 255, p.lowerS);
    lowerVSpin_ = makeSpin(0, 255, p.lowerV);
    upperHSpin_ = makeSpin(0, 180, p.upperH);
    upperSSpin_ = makeSpin(0, 255, p.upperS);
    upperVSpin_ = makeSpin(0, 255, p.upperV);

    QLabel* lblL = new QLabel(QStringLiteral("Lower Bounds"));
    lblL->setStyleSheet(
        QStringLiteral("color: %1; font: 8pt monospace; padding: 1px 2px;").arg(pal_.muted));
    layout_->addWidget(lblL);
    QHBoxLayout* rowL = new QHBoxLayout();
    rowL->setSpacing(4);
    for (const auto& [name, spin] : std::vector<std::pair<QString, QSpinBox*>>{
             {QStringLiteral("H"), lowerHSpin_},
             {QStringLiteral("S"), lowerSSpin_},
             {QStringLiteral("V"), lowerVSpin_}}) {
        QLabel* sub = subLabel(name);
        lowerSubs_.push_back(sub);
        rowL->addWidget(sub);
        rowL->addWidget(spin);
    }
    layout_->addLayout(rowL);

    QLabel* lblU = new QLabel(QStringLiteral("Upper Bounds"));
    lblU->setStyleSheet(
        QStringLiteral("color: %1; font: 8pt monospace; padding: 1px 2px;").arg(pal_.muted));
    layout_->addWidget(lblU);
    QHBoxLayout* rowU = new QHBoxLayout();
    rowU->setSpacing(4);
    for (const auto& [name, spin] : std::vector<std::pair<QString, QSpinBox*>>{
             {QStringLiteral("H"), upperHSpin_},
             {QStringLiteral("S"), upperSSpin_},
             {QStringLiteral("V"), upperVSpin_}}) {
        QLabel* sub = subLabel(name);
        upperSubs_.push_back(sub);
        rowU->addWidget(sub);
        rowU->addWidget(spin);
    }
    layout_->addLayout(rowU);

    invertCheck_ = new QCheckBox(QStringLiteral("Invert"));
    invertCheck_->setChecked(p.invert);
    connect(invertCheck_, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState) { emitParams(); });
    layout_->addWidget(invertCheck_);
    layout_->addWidget(hint(QStringLiteral(
        "InRange mask in chosen color space. Adjust for object isolation.")));
    layout_->addStretch();
    updateColorThresholdLabels();
}

void FilterPanel::buildRotate(const FilterParams& p) {
    layout_->addWidget(titleLabel(QStringLiteral("ROTATE IMAGE")));
    angleSpin_ = makeSpin(-180, 180, p.angle);
    stacked(QStringLiteral("Angle (degrees, CCW)"), {angleSpin_});
    layout_->addWidget(hint(QStringLiteral(
        "Rotates the frame counter-clockwise.\nUse 90° steps for portrait mounting.")));
    layout_->addStretch();
}

void FilterPanel::updateColorThresholdLabels() {
    if (!colorSpaceCombo_) return;
    const QString cs = colorSpaceCombo_->currentData().toString();
    std::vector<QString> names;
    int hmax = 255;
    if (cs == QStringLiteral("HSV")) { names = {QStringLiteral("H"), QStringLiteral("S"), QStringLiteral("V")}; hmax = 180; }
    else if (cs == QStringLiteral("LAB")) { names = {QStringLiteral("L"), QStringLiteral("A"), QStringLiteral("B")}; }
    else { names = {QStringLiteral("Y"), QStringLiteral("Cr"), QStringLiteral("Cb")}; }
    for (size_t i = 0; i < names.size() && i < lowerSubs_.size(); ++i) lowerSubs_[i]->setText(names[i]);
    for (size_t i = 0; i < names.size() && i < upperSubs_.size(); ++i) upperSubs_[i]->setText(names[i]);
    if (lowerHSpin_) lowerHSpin_->setRange(0, hmax);
    if (upperHSpin_) {
        upperHSpin_->setRange(0, hmax);
        if (upperHSpin_->value() > hmax) upperHSpin_->setValue(hmax);
    }
}
