#!/usr/bin/env python3
"""opencam - Beautiful Omarchy-native USB webcam + C-mount lens setup tool.

Live preview + clean filters (Binary Threshold with interactive histogram,
Sobel X with kernel controls). Dark theme matching Omarchy.
Sidebar layout for controls + explicit camera path.
"""

import glob
import os
import subprocess
import sys
import time
from typing import Optional, Tuple

import cv2
import numpy as np
from PyQt6.QtCore import (
    QPoint, QMutex, QMutexLocker, QObject, Qt, QThread, QTimer, pyqtSignal, pyqtSlot
)
from PyQt6.QtGui import (
    QBrush, QColor, QFont, QImage, QPainter, QPen, QPixmap, QPalette
)
from PyQt6.QtWidgets import (
    QApplication, QCheckBox, QComboBox, QDoubleSpinBox, QFrame, QHBoxLayout, QLabel, QMainWindow,
    QMessageBox, QPushButton, QSpinBox, QVBoxLayout, QWidget,
    QSizePolicy, QListWidget, QListWidgetItem, QScrollArea
)

import tomllib

APP_VERSION = "0.1"

def load_omarchy_palette() -> dict:
    """Load colors from active Omarchy theme for full compatibility."""
    defaults = {
        "BG": "#1a1b26",
        "DARK_BG": "#13141c",
        "LIGHTER_BG": "#24283b",
        "FG": "#a9b1d6",
        "MUTED": "#565f89",
        "ACCENT": "#7aa2f7",
        "BORDER": "#414868",
        "RED_LINE": "#f7768e",
        "HIST_BAR": "#7aa2f7",
        "ACCENT_HOVER": "#9ece6a",
    }
    try:
        slug = subprocess.check_output(
            ["omarchy", "theme", "current"],
            stderr=subprocess.DEVNULL,
            timeout=2,
        ).decode().strip().lower().replace(" ", "-")
        for base in (
            os.path.expanduser("~/.config/omarchy/themes"),
            "/usr/share/omarchy/themes",
        ):
            p = os.path.join(base, slug, "colors.toml")
            if os.path.isfile(p):
                with open(p, "rb") as f:
                    data = tomllib.load(f)
                def get(k, fb):
                    v = data.get(k, fb)
                    if isinstance(v, str):
                        v = v.strip().strip('"').strip("'")
                    return v or fb
                return {
                    "BG": get("background", defaults["BG"]),
                    "DARK_BG": get("dark_background", defaults["DARK_BG"]),
                    "LIGHTER_BG": get("lighter_background", defaults["LIGHTER_BG"]),
                    "FG": get("foreground", defaults["FG"]),
                    "MUTED": get("muted", defaults["MUTED"]),
                    "ACCENT": get("accent", defaults["ACCENT"]),
                    "BORDER": get("selection", defaults["BORDER"]),
                    "RED_LINE": defaults["RED_LINE"],
                    "HIST_BAR": get("cyan", defaults["HIST_BAR"]),
                    "ACCENT_HOVER": defaults["ACCENT_HOVER"],
                }
    except Exception:
        pass
    return defaults

_PAL = load_omarchy_palette()
BG = _PAL["BG"]
DARK_BG = _PAL["DARK_BG"]
LIGHTER_BG = _PAL["LIGHTER_BG"]
FG = _PAL["FG"]
MUTED = _PAL["MUTED"]
ACCENT = _PAL["ACCENT"]
ACCENT_HOVER = _PAL["ACCENT_HOVER"]
BORDER = _PAL["BORDER"]
RED_LINE = _PAL["RED_LINE"]
HIST_BAR = _PAL["HIST_BAR"]

FILTERS = [
    ("None", "none"),
    ("Binary Threshold", "binary"),
    ("Gaussian Smoothing", "gaussian"),
    ("Sobel X", "sobelx"),
    ("Image Color", "color"),
    ("Morphological", "morph"),
    ("Canny", "canny"),
    ("Median Blur", "median"),
    ("Bilateral Filter", "bilateral"),
    ("Color Threshold", "color_threshold"),
    ("Rotate Image", "rotate"),
]

KERNEL_SIZES = [3, 5, 7]


def list_usb_cameras() -> list[Tuple[int, str, str]]:
    """Detect V4L2 cameras. Prefer USB, return [(index, label, path), ...]."""
    cameras: list[Tuple[int, str, str]] = []
    try:
        out = subprocess.check_output(
            ["v4l2-ctl", "--list-devices"],
            stderr=subprocess.DEVNULL,
            timeout=2,
        ).decode("utf-8", errors="ignore")
    except Exception:
        out = ""

    current_name = ""
    for line in out.splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith("/dev/video"):
            try:
                idx = int(line.split("video")[-1])
                path = line
                label = current_name or f"Camera {idx}"
                cameras.append((idx, f"{idx}: {label}", path))
            except ValueError:
                continue
        elif ":" in line and not line.startswith("/dev"):
            current_name = line.split(":", 1)[0].strip()[:48]

    if not cameras:
        for dev in sorted(glob.glob("/dev/video[0-9]*")):
            try:
                idx = int(dev.split("video")[-1])
                cameras.append((idx, f"{idx}: /dev/video{idx}", dev))
            except ValueError:
                continue

    def score(item):
        _, lbl, pth = item
        l = (lbl + " " + pth).lower()
        s = 0
        if "usb" in l: s += 10
        if "hd" in l or "c-mount" in l or "webcam" in l: s += 5
        if "integrated" in l or "internal" in l: s -= 5
        return -s
    cameras.sort(key=score)
    return cameras


def frame_to_qimage(frame: np.ndarray) -> QImage:
    if frame is None or frame.size == 0:
        return QImage()
    if len(frame.shape) == 2:
        h, w = frame.shape
        return QImage(frame.data, w, h, w, QImage.Format.Format_Grayscale8).copy()
    rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    h, w, ch = rgb.shape
    bytes_per_line = ch * w
    return QImage(rgb.data, w, h, bytes_per_line, QImage.Format.Format_RGB888).copy()


def apply_pipeline(frame: np.ndarray, pipeline: list, selected_idx: int = -1, upto=None):
    """Run the filter chain on a raw BGR frame.

    Returns (display_bgr, hist_data). Only stages up to and including `upto`
    are applied (None = whole chain), so clicking a stage shows the cumulative
    result 1..N. hist_data is computed from the input of the SELECTED binary
    stage (or None). Shared by the live worker and the stopped-stream still
    renderer so both show identical results.
    """
    if frame is None:
        return None, None
    stages = pipeline or []
    if upto is not None and upto >= 0:
        stages = stages[:upto + 1]
    current = frame.copy()
    hist_data = None
    for i, f in enumerate(stages):
        if i == selected_idx and f.get('type') == 'binary':
            gray = cv2.cvtColor(current, cv2.COLOR_BGR2GRAY)
            hist, _ = np.histogram(gray.flatten(), bins=256, range=[0, 256])
            hist_data = (hist, f.get('params', {}).get('thresh', 128))
        current = CameraWorker._apply_filter(current, f.get('type'), f.get('params', {}))
    return current, hist_data


class CameraWorker(QObject):
    frame_ready = pyqtSignal(QImage, object)
    error = pyqtSignal(str)
    fps_ready = pyqtSignal(float)
    finished = pyqtSignal()

    def __init__(self, cam_index: int, pipeline: list = None, selected_idx: int = -1):
        super().__init__()
        self.cam_index = cam_index
        self.pipeline = list(pipeline) if pipeline else []
        self.selected_idx = selected_idx
        self.running = False
        self.mutex = QMutex()
        self.cap: Optional[cv2.VideoCapture] = None
        self.frame_count = 0
        self.last_fps_time = time.time()
        self.last_frame_count = 0
        self.last_raw: Optional[np.ndarray] = None

    @pyqtSlot(int, list, int)
    def update_settings(self, cam_index: int, pipeline: list, selected_idx: int = -1):
        with QMutexLocker(self.mutex):
            need_restart = cam_index != self.cam_index
            self.cam_index = cam_index
            self.pipeline = list(pipeline) if pipeline else []
            self.selected_idx = selected_idx
            if need_restart and self.running:
                self._restart_capture()

    def _restart_capture(self):
        if self.cap:
            self.cap.release()
            self.cap = None
        self._open_capture()

    def _open_capture(self):
        self.cap = cv2.VideoCapture(self.cam_index, cv2.CAP_V4L2)
        if not self.cap or not self.cap.isOpened():
            self.error.emit(f"Failed to open camera {self.cam_index}")
            self.cap = None
            return
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
        self.cap.set(cv2.CAP_PROP_FPS, 30)
        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

    def run(self):
        self.running = True
        self._open_capture()
        if not self.cap:
            self.running = False
            return

        while self.running:
            if not self.cap or not self.cap.isOpened():
                self.error.emit("Camera disconnected")
                break

            ret, frame = self.cap.read()
            if not ret or frame is None:
                time.sleep(0.05)
                continue

            with QMutexLocker(self.mutex):
                pipe = list(self.pipeline)
                sel_idx = self.selected_idx

            upto = sel_idx if 0 <= sel_idx < len(pipe) else None
            display, hist_data = apply_pipeline(frame, pipe, sel_idx, upto=upto)
            self.last_raw = frame  # frame is never mutated (apply_pipeline copies)
            qimg = frame_to_qimage(display)

            self.frame_count += 1
            now = time.time()
            if now - self.last_fps_time >= 1.0:
                fps = (self.frame_count - self.last_frame_count) / (now - self.last_fps_time)
                self.fps_ready.emit(round(fps, 1))
                self.last_fps_time = now
                self.last_frame_count = self.frame_count

            self.frame_ready.emit(qimg, hist_data)
            time.sleep(0.01)

        if self.cap:
            self.cap.release()
            self.cap = None
        self.finished.emit()

    @staticmethod
    def _apply_filter(frame: np.ndarray, filt: str, p: dict) -> np.ndarray:
        if filt == "none" or frame is None:
            return frame

        if filt == "color":
            mode = p.get("mode", "color")
            if mode == "gray":
                gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
                return cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
            return frame

        if filt == "color_threshold":
            cs = p.get("color_space", "HSV")
            if cs == "HSV":
                lower = np.array([p.get("lower_h", 0), p.get("lower_s", 0), p.get("lower_v", 0)])
                upper = np.array([p.get("upper_h", 180), p.get("upper_s", 255), p.get("upper_v", 255)])
                conv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
            elif cs == "LAB":
                lower = np.array([p.get("lower_h", 0), p.get("lower_s", 0), p.get("lower_v", 0)])
                upper = np.array([p.get("upper_h", 255), p.get("upper_s", 255), p.get("upper_v", 255)])
                conv = cv2.cvtColor(frame, cv2.COLOR_BGR2LAB)
            else:
                lower = np.array([p.get("lower_h", 0), p.get("lower_s", 0), p.get("lower_v", 0)])
                upper = np.array([p.get("upper_h", 255), p.get("upper_s", 255), p.get("upper_v", 255)])
                conv = cv2.cvtColor(frame, cv2.COLOR_BGR2YCrCb)
            mask = cv2.inRange(conv, lower, upper)
            if p.get("invert", False):
                mask = 255 - mask
            return cv2.cvtColor(mask, cv2.COLOR_GRAY2BGR)

        if filt == "rotate":
            angle = float(p.get("angle", 0))
            norm = angle % 360
            if norm == 0:
                return frame  # neutral: zero-cost passthrough (never mutated downstream)
            if norm == 180:
                return cv2.rotate(frame, cv2.ROTATE_180)
            if norm == 90:
                return cv2.rotate(frame, cv2.ROTATE_90_COUNTERCLOCKWISE)
            if norm == 270:
                return cv2.rotate(frame, cv2.ROTATE_90_CLOCKWISE)
            h, w = frame.shape[:2]
            M = cv2.getRotationMatrix2D((w / 2, h / 2), angle, 1.0)
            return cv2.warpAffine(frame, M, (w, h))

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

        if filt == "binary":
            t = int(p.get("thresh", 128))
            _, binary = cv2.threshold(gray, t, 255, cv2.THRESH_BINARY)
            return cv2.cvtColor(binary, cv2.COLOR_GRAY2BGR)

        if filt == "gaussian":
            k = int(p.get("kernel", 5))
            if k % 2 == 0:
                k = 5
            passes = int(p.get("passes", 1))
            for _ in range(passes):
                gray = cv2.GaussianBlur(gray, (k, k), 0)
            return cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)

        if filt == "sobelx":
            k = int(p.get("kernel", 3))
            if k % 2 == 0:
                k = 3
            sob = cv2.Sobel(gray, cv2.CV_64F, 1, 0, ksize=k)
            sob = cv2.convertScaleAbs(sob)
            return cv2.cvtColor(sob, cv2.COLOR_GRAY2BGR)

        if filt == "morph":
            op_str = p.get("operation", "OPEN")
            shape_str = p.get("kernel_shape", "RECT")
            ksize = int(p.get("kernel_size", 5))
            if ksize % 2 == 0:
                ksize = 5
            iters = int(p.get("iterations", 1))
            op_map = {
                "ERODE": cv2.MORPH_ERODE,
                "DILATE": cv2.MORPH_DILATE,
                "OPEN": cv2.MORPH_OPEN,
                "CLOSE": cv2.MORPH_CLOSE,
                "GRADIENT": cv2.MORPH_GRADIENT,
                "TOPHAT": cv2.MORPH_TOPHAT,
                "BLACKHAT": cv2.MORPH_BLACKHAT,
            }
            shape_map = {
                "RECT": cv2.MORPH_RECT,
                "ELLIPSE": cv2.MORPH_ELLIPSE,
                "CROSS": cv2.MORPH_CROSS,
            }
            kernel = cv2.getStructuringElement(shape_map[shape_str], (ksize, ksize))
            res = cv2.morphologyEx(gray, op_map[op_str], kernel, iterations=iters)
            return cv2.cvtColor(res, cv2.COLOR_GRAY2BGR)

        if filt == "canny":
            t1 = int(p.get("threshold1", 50))
            t2 = int(p.get("threshold2", 150))
            ap = int(p.get("aperture_size", 3))
            l2 = bool(p.get("L2gradient", False))
            edges = cv2.Canny(gray, t1, t2, apertureSize=ap, L2gradient=l2)
            return cv2.cvtColor(edges, cv2.COLOR_GRAY2BGR)

        if filt == "median":
            k = int(p.get("kernel_size", 5))
            if k % 2 == 0:
                k = 5
            m = cv2.medianBlur(gray, k)
            return cv2.cvtColor(m, cv2.COLOR_GRAY2BGR)

        if filt == "bilateral":
            d = int(p.get("d", 9))
            sc = float(p.get("sigma_color", 75))
            ss = float(p.get("sigma_space", 75))
            b = cv2.bilateralFilter(gray, d, sc, ss)
            return cv2.cvtColor(b, cv2.COLOR_GRAY2BGR)

        return frame

    def stop(self):
        self.running = False


class VideoDisplay(QWidget):
    def __init__(self):
        super().__init__()
        self.setMinimumSize(320, 240)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self._pixmap: Optional[QPixmap] = None
        self._placeholder = "No stream — select camera and press Start"
        self.setStyleSheet(f"background: {DARK_BG}; border: 1px solid {BORDER}; border-radius: 6px;")

    def set_frame(self, qimg: QImage):
        if qimg.isNull():
            self._pixmap = None
        else:
            self._pixmap = QPixmap.fromImage(qimg)
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        rect = self.rect()
        painter.fillRect(rect, QColor(DARK_BG))
        if self._pixmap and not self._pixmap.isNull():
            scaled = self._pixmap.scaled(
                rect.size(), Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation
            )
            x = (rect.width() - scaled.width()) // 2
            y = (rect.height() - scaled.height()) // 2
            painter.drawPixmap(x, y, scaled)
        else:
            painter.setPen(QColor(MUTED))
            painter.setFont(QFont("monospace", 11))
            painter.drawText(rect, Qt.AlignmentFlag.AlignCenter, self._placeholder)
        painter.setPen(QPen(QColor(BORDER), 1))
        painter.drawRect(rect.adjusted(1, 1, -1, -1))


class HistogramWidget(QWidget):
    threshold_changed = pyqtSignal(int)

    def __init__(self):
        super().__init__()
        self.setFixedHeight(118)
        self.setMinimumWidth(200)
        self.setStyleSheet(f"background: {DARK_BG}; border: 1px solid {BORDER}; border-radius: 4px;")
        self._hist: Optional[np.ndarray] = None
        self._thresh = 128
        self._dragging = False

    def set_data(self, hist: np.ndarray, thresh: int):
        self._hist = hist
        self._thresh = int(np.clip(thresh, 0, 255))
        self.update()

    def set_threshold(self, t: int):
        self._thresh = int(np.clip(t, 0, 255))
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        w, h = self.width(), self.height()
        painter.fillRect(self.rect(), QColor(DARK_BG))
        if self._hist is None or len(self._hist) == 0:
            painter.setPen(QColor(MUTED))
            painter.setFont(QFont("monospace", 9))
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "Histogram (Binary only)")
            return
        hist = self._hist.astype(float)
        maxv = hist.max() or 1.0
        hist /= maxv
        bar_width = max(1, w / 256.0)
        base_y = h - 8
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QBrush(QColor(HIST_BAR)))
        for i, val in enumerate(hist):
            bh = int(val * (h - 22))
            x = int(i * bar_width)
            painter.drawRect(x, base_y - bh, max(1, int(bar_width) - 1), bh)
        x = int((self._thresh / 255.0) * (w - 1))
        painter.setPen(QPen(QColor(RED_LINE), 2))
        painter.drawLine(x, 6, x, base_y)
        painter.setBrush(QColor(RED_LINE))
        painter.drawPolygon([QPoint(x - 4, 4), QPoint(x + 4, 4), QPoint(x, 12)])
        painter.setPen(QColor(FG))
        painter.setFont(QFont("monospace", 8))
        painter.drawText(4, 12, "0")
        painter.drawText(w - 18, 12, "255")
        painter.drawText(x - 12, h - 2, str(self._thresh))

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self._dragging = True
            self._update_from_x(event.position().x())

    def mouseMoveEvent(self, event):
        if self._dragging:
            self._update_from_x(event.position().x())

    def mouseReleaseEvent(self, event):
        self._dragging = False

    def _update_from_x(self, x: float):
        w = max(1, self.width() - 1)
        t = int(round((x / w) * 255))
        t = max(0, min(255, t))
        if t != self._thresh:
            self._thresh = t
            self.update()
            self.threshold_changed.emit(t)


class FilterPanel(QWidget):
    params_changed = pyqtSignal(int, dict)  # idx, params  (-1 for none)

    def __init__(self):
        super().__init__()
        self.setMinimumWidth(0)
        self.setMaximumWidth(280)
        self.layout = QVBoxLayout(self)
        self.layout.setContentsMargins(8, 8, 8, 8)
        self.layout.setSpacing(6)
        self.current_key = "none"
        self.current_idx = -1
        self._build_none()

    def resizeEvent(self, event):
        # The QScrollArea resizes this panel without re-running its layout
        # (children keep stale geometry) — force a re-layout on every resize.
        super().resizeEvent(event)
        try:
            self.layout.activate()
        except RuntimeError:
            pass

    def set_filter(self, key: str, params: dict = None, idx: int = -1):
        self.current_key = key
        self.current_idx = idx
        while self.layout.count():
            item = self.layout.takeAt(0)
            if w := item.widget():
                w.deleteLater()
        p = params or {}
        if key == "binary":
            self._build_binary(p)
        elif key == "gaussian":
            self._build_gaussian(p)
        elif key == "sobelx":
            self._build_sobel(p)
        elif key == "color":
            self._build_color(p)
        elif key == "morph":
            self._build_morph(p)
        elif key == "canny":
            self._build_canny(p)
        elif key == "median":
            self._build_median(p)
        elif key == "bilateral":
            self._build_bilateral(p)
        elif key == "color_threshold":
            self._build_color_threshold(p)
        elif key == "rotate":
            self._build_rotate(p)
        else:
            self._build_none()
        # Newly built children don't reliably trigger a layout pass inside the
        # scroll area — run one synchronously at the current panel width.
        self.layout.activate()
        QTimer.singleShot(20, lambda: self.params_changed.emit(self.current_idx, self._current_params()))

    def _current_params(self) -> dict:
        if self.current_key == "binary":
            return {"thresh": self.thresh_spin.value()}
        if self.current_key == "gaussian":
            return {
                "kernel": self.kernel_combo.currentData() or 5,
                "passes": self.passes_spin.value() if hasattr(self, "passes_spin") else 1
            }
        if self.current_key == "sobelx":
            return {"kernel": self.kernel_combo.currentData() or 3}
        if self.current_key == "color":
            return {"mode": self.mode_combo.currentData() or "color"}
        if self.current_key == "morph":
            return {
                "operation": self.operation_combo.currentData() or "OPEN",
                "kernel_shape": self.kernel_shape_combo.currentData() or "RECT",
                "kernel_size": self.kernel_size_spin.value() if hasattr(self, "kernel_size_spin") else 5,
                "iterations": self.iterations_spin.value() if hasattr(self, "iterations_spin") else 1
            }
        if self.current_key == "canny":
            return {
                "threshold1": self.thresh1_spin.value() if hasattr(self, "thresh1_spin") else 50,
                "threshold2": self.thresh2_spin.value() if hasattr(self, "thresh2_spin") else 150,
                "aperture_size": self.aperture_combo.currentData() or 3,
                "L2gradient": self.l2_cb.isChecked() if hasattr(self, "l2_cb") else False
            }
        if self.current_key == "median":
            return {"kernel_size": self.kernel_size_spin.value() if hasattr(self, "kernel_size_spin") else 5}
        if self.current_key == "bilateral":
            return {
                "d": self.d_spin.value() if hasattr(self, "d_spin") else 9,
                "sigma_color": self.sigma_color_spin.value() if hasattr(self, "sigma_color_spin") else 75.0,
                "sigma_space": self.sigma_space_spin.value() if hasattr(self, "sigma_space_spin") else 75.0
            }
        if self.current_key == "color_threshold":
            return {
                "color_space": self.color_space_combo.currentData() or "HSV",
                "lower_h": self.lower_h_spin.value() if hasattr(self, "lower_h_spin") else 0,
                "lower_s": self.lower_s_spin.value() if hasattr(self, "lower_s_spin") else 0,
                "lower_v": self.lower_v_spin.value() if hasattr(self, "lower_v_spin") else 0,
                "upper_h": self.upper_h_spin.value() if hasattr(self, "upper_h_spin") else 180,
                "upper_s": self.upper_s_spin.value() if hasattr(self, "upper_s_spin") else 255,
                "upper_v": self.upper_v_spin.value() if hasattr(self, "upper_v_spin") else 255,
                "invert": self.invert_cb.isChecked() if hasattr(self, "invert_cb") else False
            }
        if self.current_key == "rotate":
            return {"angle": self.angle_spin.value() if hasattr(self, "angle_spin") else 0}
        return {}

    def _build_none(self):
        lbl = QLabel("No extra controls for this filter.\nRaw camera view.")
        lbl.setStyleSheet(f"color: {MUTED}; font: 10pt monospace;")
        lbl.setWordWrap(True)
        self.layout.addWidget(lbl)
        self.layout.addStretch()

    def _hint(self, text: str) -> QLabel:
        lbl = QLabel(text)
        lbl.setStyleSheet(f"color: {MUTED}; font: 8pt monospace;")
        lbl.setWordWrap(True)
        return lbl

    def _stacked(self, label_text: str, *fields):
        """Caption label above a row of control(s) — keeps the panel narrow."""
        lbl = QLabel(label_text)
        lbl.setStyleSheet(f"color: {MUTED}; font: 8pt monospace; padding: 1px 2px;")
        self.layout.addWidget(lbl)
        row = QHBoxLayout()
        row.setSpacing(4)
        for f in fields:
            if isinstance(f, str):
                sub = QLabel(f)
                sub.setStyleSheet(f"color: {FG}; font: 9pt monospace;")
                row.addWidget(sub)
            else:
                row.addWidget(f)
        self.layout.addLayout(row)
        return row

    def _build_binary(self, p: dict = None):
        p = p or {}
        title = QLabel("BINARY THRESHOLD")
        title.setStyleSheet(f"color: {ACCENT}; font: bold 10pt monospace; letter-spacing: 1px;")
        self.layout.addWidget(title)
        self.thresh_spin = QSpinBox()
        self.thresh_spin.setRange(0, 255)
        self.thresh_spin.setValue(p.get("thresh", 128))
        self.thresh_spin.setSingleStep(1)
        self.thresh_spin.setStyleSheet(self._spin_style())
        self.thresh_spin.valueChanged.connect(self._emit_params)
        self._stacked("Threshold", self.thresh_spin)
        self.hist = HistogramWidget()
        self.hist.threshold_changed.connect(self._sync_from_hist)
        self.thresh_spin.valueChanged.connect(lambda v: self.hist.set_threshold(v))
        self.layout.addWidget(self.hist)
        hint = self._hint("Drag the red line or type value.\nHelps tune C-mount focus contrast.")
        self.layout.addWidget(hint)
        self.layout.addStretch()

    def _build_gaussian(self, p: dict = None):
        p = p or {}
        title = QLabel("GAUSSIAN SMOOTHING")
        title.setStyleSheet(f"color: {ACCENT}; font: bold 10pt monospace; letter-spacing: 1px;")
        self.layout.addWidget(title)

        self.kernel_combo = QComboBox()
        for k in [3, 5, 7, 9]:
            self.kernel_combo.addItem(f"{k}×{k}", k)
        kdef = p.get("kernel", 5)
        idx = [3,5,7,9].index(kdef) if kdef in [3,5,7,9] else 1
        self.kernel_combo.setCurrentIndex(idx)
        self.kernel_combo.setStyleSheet(self._combo_style())
        self.kernel_combo.currentIndexChanged.connect(self._emit_params)
        self._stacked("Kernel Size", self.kernel_combo)

        self.passes_spin = QSpinBox()
        self.passes_spin.setRange(1, 5)
        self.passes_spin.setValue(p.get("passes", 1))
        self.passes_spin.setStyleSheet(self._spin_style())
        self.passes_spin.valueChanged.connect(self._emit_params)
        self._stacked("Passes", self.passes_spin)

        hint = self._hint("Applies Gaussian blur multiple times.\nHigher passes = stronger smoothing for lens setup.")
        self.layout.addWidget(hint)
        self.layout.addStretch()

    def _build_sobel(self, p: dict = None):
        p = p or {}
        title = QLabel("SOBEL X (VERTICAL EDGES)")
        title.setStyleSheet(f"color: {ACCENT}; font: bold 10pt monospace; letter-spacing: 1px;")
        self.layout.addWidget(title)
        self.kernel_combo = QComboBox()
        for k in KERNEL_SIZES:
            self.kernel_combo.addItem(f"{k}×{k}", k)
        kdef = p.get("kernel", 3)
        try:
            idx = KERNEL_SIZES.index(kdef)
        except ValueError:
            idx = 0
        self.kernel_combo.setCurrentIndex(idx)
        self.kernel_combo.setStyleSheet(self._combo_style())
        self.kernel_combo.currentIndexChanged.connect(self._emit_params)
        self._stacked("Kernel", self.kernel_combo)
        hint = self._hint("Detects horizontal intensity changes.\nUseful for checking lens alignment\nand fine focus on edges.")
        self.layout.addWidget(hint)
        self.layout.addStretch()

    def _build_color(self, p: dict = None):
        p = p or {}
        title = QLabel("IMAGE COLOR")
        title.setStyleSheet(f"color: {ACCENT}; font: bold 10pt monospace; letter-spacing: 1px;")
        self.layout.addWidget(title)

        self.mode_combo = QComboBox()
        self.mode_combo.addItem("Color", "color")
        self.mode_combo.addItem("Greyscale", "gray")
        mode = p.get("mode", "color")
        self.mode_combo.setCurrentIndex(0 if mode == "color" else 1)
        self.mode_combo.setStyleSheet(self._combo_style())
        self.mode_combo.currentIndexChanged.connect(self._emit_params)
        self._stacked("Mode", self.mode_combo)

        hint = self._hint("Switch between color and greyscale view.\n'Color' uses original if available.")
        self.layout.addWidget(hint)
        self.layout.addStretch()

    def _build_morph(self, p: dict = None):
        p = p or {}
        title = QLabel("MORPHOLOGICAL")
        title.setStyleSheet(f"color: {ACCENT}; font: bold 10pt monospace; letter-spacing: 1px;")
        self.layout.addWidget(title)

        self.operation_combo = QComboBox()
        for txt, val in [("ERODE", "ERODE"), ("DILATE", "DILATE"), ("OPEN", "OPEN"), ("CLOSE", "CLOSE"), ("GRADIENT", "GRADIENT"), ("TOPHAT", "TOPHAT"), ("BLACKHAT", "BLACKHAT")]:
            self.operation_combo.addItem(txt, val)
        ops = ["ERODE","DILATE","OPEN","CLOSE","GRADIENT","TOPHAT","BLACKHAT"]
        op = p.get("operation", "OPEN")
        self.operation_combo.setCurrentIndex(ops.index(op) if op in ops else 2)
        self.operation_combo.setStyleSheet(self._combo_style())
        self.operation_combo.currentIndexChanged.connect(self._emit_params)
        self._stacked("Operation", self.operation_combo)

        self.kernel_shape_combo = QComboBox()
        for txt, val in [("RECT", "RECT"), ("ELLIPSE", "ELLIPSE"), ("CROSS", "CROSS")]:
            self.kernel_shape_combo.addItem(txt, val)
        shapes = ["RECT","ELLIPSE","CROSS"]
        sh = p.get("kernel_shape", "RECT")
        self.kernel_shape_combo.setCurrentIndex(shapes.index(sh) if sh in shapes else 0)
        self.kernel_shape_combo.setStyleSheet(self._combo_style())
        self.kernel_shape_combo.currentIndexChanged.connect(self._emit_params)
        self._stacked("Kernel Shape", self.kernel_shape_combo)

        self.kernel_size_spin = QSpinBox()
        self.kernel_size_spin.setRange(3, 21)
        self.kernel_size_spin.setSingleStep(2)
        self.kernel_size_spin.setValue(p.get("kernel_size", 5))
        self.kernel_size_spin.setStyleSheet(self._spin_style())
        self.kernel_size_spin.valueChanged.connect(self._emit_params)
        self._stacked("Kernel Size (odd)", self.kernel_size_spin)

        self.iterations_spin = QSpinBox()
        self.iterations_spin.setRange(1, 10)
        self.iterations_spin.setValue(p.get("iterations", 1))
        self.iterations_spin.setStyleSheet(self._spin_style())
        self.iterations_spin.valueChanged.connect(self._emit_params)
        self._stacked("Iterations", self.iterations_spin)

        hint = self._hint("Morph ops for noise removal, edge detection etc.")
        self.layout.addWidget(hint)
        self.layout.addStretch()

    def _build_canny(self, p: dict = None):
        p = p or {}
        title = QLabel("CANNY EDGE")
        title.setStyleSheet(f"color: {ACCENT}; font: bold 10pt monospace; letter-spacing: 1px;")
        self.layout.addWidget(title)

        self.thresh1_spin = QSpinBox()
        self.thresh1_spin.setRange(0, 255)
        self.thresh1_spin.setValue(p.get("threshold1", 50))
        self.thresh1_spin.setStyleSheet(self._spin_style())
        self.thresh1_spin.valueChanged.connect(self._emit_params)
        self._stacked("Thresh 1", self.thresh1_spin)

        self.thresh2_spin = QSpinBox()
        self.thresh2_spin.setRange(0, 255)
        self.thresh2_spin.setValue(p.get("threshold2", 150))
        self.thresh2_spin.setStyleSheet(self._spin_style())
        self.thresh2_spin.valueChanged.connect(self._emit_params)
        self._stacked("Thresh 2", self.thresh2_spin)

        self.aperture_combo = QComboBox()
        for k in [3,5,7]:
            self.aperture_combo.addItem(str(k), k)
        ap = p.get("aperture_size", 3)
        aps = [3,5,7]
        self.aperture_combo.setCurrentIndex(aps.index(ap) if ap in aps else 0)
        self.aperture_combo.setStyleSheet(self._combo_style())
        self.aperture_combo.currentIndexChanged.connect(self._emit_params)
        self._stacked("Aperture", self.aperture_combo)

        self.l2_cb = QCheckBox("L2 Gradient")
        self.l2_cb.setChecked(p.get("L2gradient", False))
        self.l2_cb.stateChanged.connect(self._emit_params)
        self.layout.addWidget(self.l2_cb)

        hint = self._hint("Edge detection. Adjust thresholds for best edges.")
        self.layout.addWidget(hint)
        self.layout.addStretch()

    def _build_median(self, p: dict = None):
        p = p or {}
        title = QLabel("MEDIAN BLUR")
        title.setStyleSheet(f"color: {ACCENT}; font: bold 10pt monospace; letter-spacing: 1px;")
        self.layout.addWidget(title)

        self.kernel_size_spin = QSpinBox()
        self.kernel_size_spin.setRange(3, 15)
        self.kernel_size_spin.setSingleStep(2)
        self.kernel_size_spin.setValue(p.get("kernel_size", 5))
        self.kernel_size_spin.setStyleSheet(self._spin_style())
        self.kernel_size_spin.valueChanged.connect(self._emit_params)
        self._stacked("Kernel Size (odd)", self.kernel_size_spin)

        hint = self._hint("Non-linear noise reduction. Good for salt/pepper.")
        self.layout.addWidget(hint)
        self.layout.addStretch()

    def _build_bilateral(self, p: dict = None):
        p = p or {}
        title = QLabel("BILATERAL FILTER")
        title.setStyleSheet(f"color: {ACCENT}; font: bold 10pt monospace; letter-spacing: 1px;")
        self.layout.addWidget(title)

        self.d_spin = QSpinBox()
        self.d_spin.setRange(3, 15)
        self.d_spin.setValue(p.get("d", 9))
        self.d_spin.setStyleSheet(self._spin_style())
        self.d_spin.valueChanged.connect(self._emit_params)
        self._stacked("Diameter (d)", self.d_spin)

        self.sigma_color_spin = QDoubleSpinBox()
        self.sigma_color_spin.setRange(10, 150)
        self.sigma_color_spin.setValue(p.get("sigma_color", 75))
        self.sigma_color_spin.setSingleStep(5)
        self.sigma_color_spin.setStyleSheet(self._spin_style())
        self.sigma_color_spin.valueChanged.connect(self._emit_params)
        self._stacked("Sigma Color", self.sigma_color_spin)

        self.sigma_space_spin = QDoubleSpinBox()
        self.sigma_space_spin.setRange(10, 150)
        self.sigma_space_spin.setValue(p.get("sigma_space", 75))
        self.sigma_space_spin.setSingleStep(5)
        self.sigma_space_spin.setStyleSheet(self._spin_style())
        self.sigma_space_spin.valueChanged.connect(self._emit_params)
        self._stacked("Sigma Space", self.sigma_space_spin)

        hint = self._hint("Edge-preserving smooth. Higher sigma = stronger effect.")
        self.layout.addWidget(hint)
        self.layout.addStretch()

    def _build_color_threshold(self, p: dict = None):
        p = p or {}
        title = QLabel("COLOR THRESHOLD (InRange)")
        title.setStyleSheet(f"color: {ACCENT}; font: bold 10pt monospace; letter-spacing: 1px;")
        self.layout.addWidget(title)

        self.color_space_combo = QComboBox()
        for txt, val in [("HSV", "HSV"), ("LAB", "LAB"), ("YCrCb", "YCrCb")]:
            self.color_space_combo.addItem(txt, val)
        cs = p.get("color_space", "HSV")
        cs_list = ["HSV","LAB","YCrCb"]
        self.color_space_combo.setCurrentIndex(cs_list.index(cs) if cs in cs_list else 0)
        self.color_space_combo.setStyleSheet(self._combo_style())
        self.color_space_combo.currentIndexChanged.connect(self._update_color_threshold_labels)
        self.color_space_combo.currentIndexChanged.connect(self._emit_params)
        self._stacked("Color Space", self.color_space_combo)

        # Lower / Upper bounds: short sub-labels keep the rows narrow
        self.lower_h_spin = QSpinBox()
        self.lower_h_spin.setRange(0, 180)
        self.lower_h_spin.setValue(p.get("lower_h", 0))
        self.lower_h_spin.setStyleSheet(self._spin_style())
        self.lower_h_spin.valueChanged.connect(self._emit_params)

        self.lower_s_spin = QSpinBox()
        self.lower_s_spin.setRange(0, 255)
        self.lower_s_spin.setValue(p.get("lower_s", 0))
        self.lower_s_spin.setStyleSheet(self._spin_style())
        self.lower_s_spin.valueChanged.connect(self._emit_params)

        self.lower_v_spin = QSpinBox()
        self.lower_v_spin.setRange(0, 255)
        self.lower_v_spin.setValue(p.get("lower_v", 0))
        self.lower_v_spin.setStyleSheet(self._spin_style())
        self.lower_v_spin.valueChanged.connect(self._emit_params)

        self.lower_subs = []
        row_l = QHBoxLayout()
        row_l.setSpacing(4)
        for name, spin in (("H", self.lower_h_spin), ("S", self.lower_s_spin), ("V", self.lower_v_spin)):
            sub = QLabel(name)
            sub.setStyleSheet(f"color: {FG}; font: 9pt monospace;")
            self.lower_subs.append(sub)
            row_l.addWidget(sub)
            row_l.addWidget(spin)
        lbl_l = QLabel("Lower Bounds")
        lbl_l.setStyleSheet(f"color: {MUTED}; font: 8pt monospace; padding: 1px 2px;")
        self.layout.addWidget(lbl_l)
        self.layout.addLayout(row_l)

        self.upper_h_spin = QSpinBox()
        self.upper_h_spin.setRange(0, 180)
        self.upper_h_spin.setValue(p.get("upper_h", 180))
        self.upper_h_spin.setStyleSheet(self._spin_style())
        self.upper_h_spin.valueChanged.connect(self._emit_params)

        self.upper_s_spin = QSpinBox()
        self.upper_s_spin.setRange(0, 255)
        self.upper_s_spin.setValue(p.get("upper_s", 255))
        self.upper_s_spin.setStyleSheet(self._spin_style())
        self.upper_s_spin.valueChanged.connect(self._emit_params)

        self.upper_v_spin = QSpinBox()
        self.upper_v_spin.setRange(0, 255)
        self.upper_v_spin.setValue(p.get("upper_v", 255))
        self.upper_v_spin.setStyleSheet(self._spin_style())
        self.upper_v_spin.valueChanged.connect(self._emit_params)

        self.upper_subs = []
        row_u = QHBoxLayout()
        row_u.setSpacing(4)
        for name, spin in (("H", self.upper_h_spin), ("S", self.upper_s_spin), ("V", self.upper_v_spin)):
            sub = QLabel(name)
            sub.setStyleSheet(f"color: {FG}; font: 9pt monospace;")
            self.upper_subs.append(sub)
            row_u.addWidget(sub)
            row_u.addWidget(spin)
        lbl_u = QLabel("Upper Bounds")
        lbl_u.setStyleSheet(f"color: {MUTED}; font: 8pt monospace; padding: 1px 2px;")
        self.layout.addWidget(lbl_u)
        self.layout.addLayout(row_u)

        self.invert_cb = QCheckBox("Invert")
        self.invert_cb.setChecked(p.get("invert", False))
        self.invert_cb.stateChanged.connect(self._emit_params)
        self.layout.addWidget(self.invert_cb)

        hint = self._hint("InRange mask in chosen color space. Adjust for object isolation.")
        self.layout.addWidget(hint)
        self.layout.addStretch()

        # init labels
        self._update_color_threshold_labels()

    def _build_rotate(self, p: dict = None):
        p = p or {}
        title = QLabel("ROTATE IMAGE")
        title.setStyleSheet(f"color: {ACCENT}; font: bold 10pt monospace; letter-spacing: 1px;")
        self.layout.addWidget(title)
        self.angle_spin = QSpinBox()
        self.angle_spin.setRange(-180, 180)
        self.angle_spin.setSingleStep(1)
        self.angle_spin.setValue(p.get("angle", 0))
        self.angle_spin.setStyleSheet(self._spin_style())
        self.angle_spin.valueChanged.connect(self._emit_params)
        self._stacked("Angle (degrees, CCW)", self.angle_spin)
        hint = self._hint("Rotates the frame counter-clockwise.\nUse 90° steps for portrait mounting.")
        self.layout.addWidget(hint)
        self.layout.addStretch()

    def _update_color_threshold_labels(self):
        if not hasattr(self, "color_space_combo"):
            return
        cs = self.color_space_combo.currentData()
        if cs == "HSV":
            names = ["H", "S", "V"]
            hmax = 180
        elif cs == "LAB":
            names = ["L", "A", "B"]
            hmax = 255
        else:  # YCrCb
            names = ["Y", "Cr", "Cb"]
            hmax = 255
        for lbl, n in zip(self.lower_subs, names):
            lbl.setText(n)
        for lbl, n in zip(self.upper_subs, names):
            lbl.setText(n)
        self.lower_h_spin.setRange(0, hmax)
        self.upper_h_spin.setRange(0, hmax)
        if self.upper_h_spin.value() > hmax:
            self.upper_h_spin.setValue(hmax)

    def _emit_params(self, *_):
        self.params_changed.emit(getattr(self, 'current_idx', -1), self._current_params())

    def _sync_from_hist(self, t: int):
        if hasattr(self, "thresh_spin"):
            self.thresh_spin.blockSignals(True)
            self.thresh_spin.setValue(t)
            self.thresh_spin.blockSignals(False)
        self._emit_params()

    def _spin_style(self):
        return f"""
            QSpinBox {{
                background: {LIGHTER_BG}; color: {FG}; border: 1px solid {BORDER};
                border-radius: 3px; padding: 2px 6px; font: 10pt monospace;
            }}
            QSpinBox:focus {{ border: 1px solid {ACCENT}; }}
        """

    def _combo_style(self):
        return f"""
            QComboBox {{
                background: {LIGHTER_BG}; color: {FG}; border: 1px solid {BORDER};
                border-radius: 3px; padding: 3px 8px; font: 10pt monospace;
            }}
            QComboBox:focus {{ border: 1px solid {ACCENT}; }}
            QComboBox::drop-down {{ border: 0; }}
        """


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("OPENCAM")
        self.setMinimumSize(820, 520)
        self.resize(980, 620)

        self.cameras = list_usb_cameras()
        self.current_cam = self.cameras[0][0] if self.cameras else 0
        self.current_path = self.cameras[0][2] if self.cameras else ""
        # The stack always opens with a pinned "No Filter" stage at the top so
        # the raw frame is one click away. It cannot be removed or moved.
        self.pipeline = [{"type": "none", "name": "No Filter", "params": {}}]
        self.selected_idx = 0
        self.streaming = False
        self.last_raw: Optional[np.ndarray] = None  # frozen raw frame for stopped-stream editing
        self.worker: Optional[CameraWorker] = None
        self.thread: Optional[QThread] = None

        self._build_ui()
        self._apply_styles()
        self._populate_cameras()
        self._rebuild_filter_list()
        self.panel.set_filter("none", {}, 0)

        self.cam_timer = QTimer(self)
        self.cam_timer.timeout.connect(self._refresh_cameras_if_needed)
        self.cam_timer.start(8000)

    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        outer_v = QVBoxLayout(central)
        outer_v.setContentsMargins(6, 6, 6, 6)
        outer_v.setSpacing(4)

        # Title bar
        title_bar = QFrame()
        title_bar.setStyleSheet(f"background: {LIGHTER_BG}; border-bottom: 1px solid {BORDER};")
        title_l = QHBoxLayout(title_bar)
        title_l.setContentsMargins(12, 6, 12, 6)
        title = QLabel("OPENCAM")
        title.setStyleSheet(f"font: bold 13pt monospace; color: {ACCENT}; letter-spacing: 2px;")
        title_l.addWidget(title)
        title_l.addStretch()
        outer_v.addWidget(title_bar)

        main_h = QHBoxLayout()
        main_h.setSpacing(6)

        # Left sidebar
        sidebar = QFrame()
        sidebar.setFixedWidth(235)
        sidebar.setFrameShape(QFrame.Shape.StyledPanel)
        side_v = QVBoxLayout(sidebar)
        side_v.setContentsMargins(8, 8, 8, 8)
        side_v.setSpacing(3)

        self.btn_toggle = QPushButton("▶  START STREAM")
        self.btn_toggle.setMinimumHeight(30)
        self.btn_toggle.clicked.connect(self._toggle_stream)
        side_v.addWidget(self.btn_toggle)

        side_v.addSpacing(6)

        side_v.addWidget(QLabel("Camera"))
        self.cam_combo = QComboBox()
        self.cam_combo.setMinimumWidth(210)
        self.cam_combo.currentIndexChanged.connect(self._on_cam_changed)
        side_v.addWidget(self.cam_combo)

        self.path_label = QLabel("Path: —")
        self.path_label.setStyleSheet(f"color: {MUTED}; font: 8pt monospace;")
        side_v.addWidget(self.path_label)

        side_v.addSpacing(4)

        side_v.addWidget(QLabel("Add Filter"))
        add_h = QHBoxLayout()
        self.add_combo = QComboBox()
        for label, key in FILTERS:
            if key != "none":
                self.add_combo.addItem(label, key)
        self.add_combo.setMinimumWidth(150)
        self.add_btn = QPushButton("Add")
        self.add_btn.setStyleSheet(self._compact_btn_style())
        self.add_btn.clicked.connect(self._add_filter)
        add_h.addWidget(self.add_combo, 1)
        add_h.addWidget(self.add_btn)
        side_v.addLayout(add_h)

        side_v.addSpacing(4)
        side_v.addWidget(QLabel("Applied Filters (numbered)"))
        self.filter_list = QListWidget()
        self.filter_list.setFixedHeight(90)
        self.filter_list.itemSelectionChanged.connect(self._on_filter_list_selection)
        side_v.addWidget(self.filter_list)

        btns_h = QHBoxLayout()
        self.remove_btn = QPushButton("✕")
        self.up_btn = QPushButton("↑")
        self.down_btn = QPushButton("↓")
        compact = self._compact_btn_style()
        for b in (self.remove_btn, self.up_btn, self.down_btn):
            b.setFixedWidth(34)
            b.setStyleSheet(compact)
        self.remove_btn.clicked.connect(self._remove_selected_filter)
        self.up_btn.clicked.connect(self._move_filter_up)
        self.down_btn.clicked.connect(self._move_filter_down)
        btns_h.addWidget(self.remove_btn)
        btns_h.addWidget(self.up_btn)
        btns_h.addWidget(self.down_btn)
        btns_h.addStretch()
        side_v.addLayout(btns_h)

        side_v.addStretch()

        self.status_lbl = QLabel("Idle")
        self.status_lbl.setStyleSheet(f"color: {MUTED}; font: 8pt monospace;")
        side_v.addWidget(self.status_lbl)

        main_h.addWidget(sidebar)

        content = QHBoxLayout()
        content.setSpacing(6)

        vcontent = QVBoxLayout()
        vcontent.setSpacing(2)
        self.pipeline_label = QLabel("Pipeline: (none)")
        self.pipeline_label.setStyleSheet(f"color: {MUTED}; font: 8pt monospace; padding: 1px 4px; background: {DARK_BG}; border: 1px solid {BORDER};")
        self.pipeline_label.setWordWrap(True)
        vcontent.addWidget(self.pipeline_label)
        self.size_label = QLabel("—")
        self.size_label.setStyleSheet(f"color: {MUTED}; font: 8pt monospace; padding: 1px 4px;")
        self.size_label.setToolTip("Current image size (height × width) after the viewed stage")
        vcontent.addWidget(self.size_label)
        self.video = VideoDisplay()
        vcontent.addWidget(self.video, 3)

        content.addLayout(vcontent, 3)

        self.panel = FilterPanel()
        self.panel.params_changed.connect(self._on_panel_params)
        panel_scroll = QScrollArea()
        panel_scroll.setWidget(self.panel)
        panel_scroll.setWidgetResizable(True)
        panel_scroll.setFrameShape(QFrame.Shape.NoFrame)
        panel_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        panel_scroll.setMinimumWidth(230)
        content.addWidget(panel_scroll, 1)

        main_h.addLayout(content, 1)

        outer_v.addLayout(main_h, 1)

        footer = QLabel(f"Tip: Use Binary + drag histogram line while adjusting the C-mount focus ring for sharp contrast.  •  opencam v{APP_VERSION}")
        footer.setStyleSheet(f"color: {MUTED}; font: 7pt monospace; padding: 1px 4px;")
        outer_v.addWidget(footer)

    def _apply_styles(self):
        self.setStyleSheet(f"""
            QMainWindow, QWidget {{ background: {BG}; color: {FG}; font-family: monospace; }}
            QLabel {{ color: {FG}; }}
            QPushButton {{
                background: {LIGHTER_BG}; color: {FG}; border: 1px solid {BORDER};
                border-radius: 4px; padding: 6px 14px; font: 10pt monospace;
            }}
            QPushButton:hover {{ border: 1px solid {ACCENT}; background: #2a2b3a; }}
            QPushButton:pressed {{ background: {ACCENT}; color: {BG}; }}
            QComboBox {{
                background: {LIGHTER_BG}; color: {FG}; border: 1px solid {BORDER};
                border-radius: 4px; padding: 4px 8px; font: 10pt monospace;
            }}
            QComboBox:focus {{ border: 1px solid {ACCENT}; }}
            QComboBox QAbstractItemView {{
                background: {DARK_BG}; color: {FG}; border: 1px solid {BORDER};
                selection-background-color: {ACCENT}; selection-color: {BG}; outline: 0;
            }}
            QComboBox QAbstractItemView::item {{
                min-height: 22px; padding: 3px 10px;
            }}
            QListWidget {{
                background: {DARK_BG}; color: {FG}; border: 1px solid {BORDER};
                border-radius: 4px; font: 9pt monospace; outline: 0;
            }}
            QListWidget::item {{ padding: 3px 6px; }}
            QListWidget::item:hover {{ background: {LIGHTER_BG}; }}
            QListWidget::item:selected {{ background: {ACCENT}; color: {BG}; }}
        """)
        self._update_toggle_style()

    def _compact_btn_style(self):
        return f"""
            QPushButton {{
                background: {LIGHTER_BG}; color: {FG}; border: 1px solid {BORDER};
                border-radius: 4px; padding: 4px 8px; font: 10pt monospace;
            }}
            QPushButton:hover {{ border: 1px solid {ACCENT}; background: #2a2b3a; }}
            QPushButton:pressed {{ background: {ACCENT}; color: {BG}; }}
        """

    def _update_toggle_style(self):
        if self.streaming:
            self.btn_toggle.setText("⏹  STOP STREAM")
            self.btn_toggle.setStyleSheet(f"""
                QPushButton {{
                    background: #3a2a2a; color: {RED_LINE}; border: 1px solid {RED_LINE};
                    border-radius: 4px; padding: 6px 14px; font: bold 10pt monospace;
                }}
                QPushButton:hover {{ background: #4a2f2f; }}
            """)
        else:
            self.btn_toggle.setText("▶  START STREAM")
            self.btn_toggle.setStyleSheet(f"""
                QPushButton {{
                    background: {LIGHTER_BG}; color: {ACCENT_HOVER}; border: 1px solid {ACCENT};
                    border-radius: 4px; padding: 6px 14px; font: 10pt monospace;
                }}
                QPushButton:hover {{ background: #2a3a2a; }}
            """)

    def _populate_cameras(self):
        self.cam_combo.blockSignals(True)
        self.cam_combo.clear()
        if not self.cameras:
            self.cam_combo.addItem("No cameras found", (-1, ""))
            self.status_lbl.setText("No V4L2 cameras detected")
            self.path_label.setText("Path: —")
        else:
            for idx, label, path in self.cameras:
                display = f"{label}   ({path})"
                self.cam_combo.addItem(display, (idx, path))
            self.cam_combo.setCurrentIndex(0)
            data = self.cam_combo.currentData() or (0, "")
            self.current_cam = data[0]
            self.current_path = data[1]
            self.path_label.setText(f"Path: {self.current_path}")
        self.cam_combo.blockSignals(False)

    def _refresh_cameras_if_needed(self):
        if self.streaming:
            return
        fresh = list_usb_cameras()
        if fresh != self.cameras:
            self.cameras = fresh
            self._populate_cameras()

    def _on_cam_changed(self, _idx):
        data = self.cam_combo.currentData()
        if isinstance(data, (list, tuple)) and len(data) >= 2:
            new_idx, new_path = data[0], data[1]
        else:
            new_idx, new_path = (data if data is not None else 0), ""
        if self.streaming:
            if new_idx is not None and new_idx != self.current_cam:
                self.current_cam = new_idx
                self.current_path = new_path
                self.path_label.setText(f"Path: {new_path}")
                self._restart_stream()
        else:
            self.current_cam = new_idx
            self.current_path = new_path
            self.path_label.setText(f"Path: {new_path}")

    def _on_panel_params(self, idx: int, params: dict):
        if 0 <= idx < len(self.pipeline):
            self.pipeline[idx]["params"] = params
            self._update_worker()
            if self.pipeline[idx]["type"] in ("binary", "gaussian", "sobelx", "morph", "canny", "bilateral", "color_threshold"):
                QTimer.singleShot(30, lambda: self.resize(max(self.width(), 820), max(self.height(), 620)))

    def _update_pipeline_label(self):
        if not self.pipeline:
            self.pipeline_label.setText("Pipeline: (none)")
        else:
            # The raw base is structural, not a choice: unnumbered, always first.
            # Real filters are numbered 1..N over non-none stages.
            parts = []
            n = 0
            for i, f in enumerate(self.pipeline):
                if f["type"] == "none":
                    part = "Raw"
                else:
                    n += 1
                    part = f"{n}:{f['name'].split()[0][:12]}"
                # Brackets mark the viewed stage: preview shows 1..N cumulatively.
                parts.append(f"[{part}]" if i == self.selected_idx else part)
            self.pipeline_label.setText(" → ".join(parts))

    def _rebuild_filter_list(self):
        self.filter_list.blockSignals(True)
        self.filter_list.clear()
        n = 0
        for i, f in enumerate(self.pipeline):
            if f["type"] == "none":
                item = QListWidgetItem("No Filter \u2014 raw base")
            else:
                n += 1
                item = QListWidgetItem(f"{n}. {f['name']}")
            item.setData(Qt.ItemDataRole.UserRole, i)
            self.filter_list.addItem(item)
        if 0 <= self.selected_idx < len(self.pipeline):
            self.filter_list.setCurrentRow(self.selected_idx)
        self.filter_list.blockSignals(False)
        self._update_pipeline_label()
        self._update_list_buttons()

    def _add_filter(self):
        key = self.add_combo.currentData()
        if not key:
            return
        name = self.add_combo.currentText()
        params = self._default_params_for(key)
        entry = {"type": key, "name": name, "params": params}
        self.pipeline.append(entry)
        self.selected_idx = len(self.pipeline) - 1
        self._rebuild_filter_list()
        self.panel.set_filter(key, params, self.selected_idx)
        self._update_worker()

    def _default_params_for(self, key: str) -> dict:
        if key == "binary":
            return {"thresh": 128}
        if key == "gaussian":
            return {"kernel": 5, "passes": 1}
        if key == "sobelx":
            return {"kernel": 3}
        if key == "color":
            return {"mode": "color"}
        if key == "morph":
            return {"operation": "OPEN", "kernel_shape": "RECT", "kernel_size": 5, "iterations": 1}
        if key == "canny":
            return {"threshold1": 50, "threshold2": 150, "aperture_size": 3, "L2gradient": False}
        if key == "median":
            return {"kernel_size": 5}
        if key == "bilateral":
            return {"d": 9, "sigma_color": 75.0, "sigma_space": 75.0}
        if key == "color_threshold":
            return {"color_space": "HSV", "lower_h": 0, "lower_s": 0, "lower_v": 0,
                    "upper_h": 180, "upper_s": 255, "upper_v": 255, "invert": False}
        if key == "rotate":
            return {"angle": 0}
        return {}

    def _on_filter_list_selection(self):
        # Never mutate widgets synchronously inside the selection-emission call
        # stack (caused SIGABRT qFatal previously) — defer via timer.
        items = self.filter_list.selectedItems()
        if not items:
            self.selected_idx = -1
        else:
            idx = items[0].data(Qt.ItemDataRole.UserRole)
            if not (isinstance(idx, int) and 0 <= idx < len(self.pipeline)):
                return
            self.selected_idx = idx
        # Latest selection wins if several timers are queued (rapid clicks).
        QTimer.singleShot(0, lambda: self._apply_selection(self.selected_idx))

    def _apply_selection(self, idx: int):
        """Show panel + cumulative preview (stages 1..idx) for a list selection."""
        if idx != self.selected_idx:
            return  # superseded by a newer selection
        if 0 <= idx < len(self.pipeline):
            f = self.pipeline[idx]
            self.panel.set_filter(f["type"], f["params"], idx)
        else:
            self.panel.set_filter("none", {}, -1)
        self._update_pipeline_label()
        self._update_list_buttons()
        self._update_worker()

    def _update_list_buttons(self):
        # The raw base (idx 0) is structural: nothing may touch it, and no
        # real filter may move above it (up needs idx > 1).
        self.remove_btn.setEnabled(self.selected_idx > 0)
        self.up_btn.setEnabled(self.selected_idx > 1)
        self.down_btn.setEnabled(self.selected_idx > 0)

    def _remove_selected_filter(self):
        if self.selected_idx <= 0 or self.selected_idx >= len(self.pipeline):
            return  # stage 1 (No Filter) is pinned at the top
        del self.pipeline[self.selected_idx]
        self.selected_idx = min(self.selected_idx, len(self.pipeline) - 1) if self.pipeline else -1
        self._rebuild_filter_list()
        if self.selected_idx >= 0:
            f = self.pipeline[self.selected_idx]
            self.panel.set_filter(f["type"], f["params"], self.selected_idx)
        else:
            self.panel.set_filter("none", {}, -1)
        self._update_worker()

    def _move_filter_up(self):
        if self.selected_idx <= 1 or self.selected_idx >= len(self.pipeline):
            return  # never move anything above the raw base
        i = self.selected_idx
        self.pipeline[i-1], self.pipeline[i] = self.pipeline[i], self.pipeline[i-1]
        self.selected_idx -= 1
        self._rebuild_filter_list()
        self.panel.set_filter(self.pipeline[self.selected_idx]["type"], self.pipeline[self.selected_idx]["params"], self.selected_idx)
        self._update_worker()

    def _move_filter_down(self):
        if self.selected_idx <= 0 or self.selected_idx >= len(self.pipeline) - 1:
            return  # stage 1 (No Filter) stays pinned at the top
        i = self.selected_idx
        self.pipeline[i+1], self.pipeline[i] = self.pipeline[i], self.pipeline[i+1]
        self.selected_idx += 1
        self._rebuild_filter_list()
        self.panel.set_filter(self.pipeline[self.selected_idx]["type"], self.pipeline[self.selected_idx]["params"], self.selected_idx)
        self._update_worker()

    def _update_worker(self):
        if self.streaming and self.worker:
            self.worker.update_settings(self.current_cam, self.pipeline, self.selected_idx)
        elif not self.streaming:
            self._render_still()

    def _render_still(self):
        """Re-apply the pipeline to the frozen last frame (stopped stream).

        Shows the cumulative result up to and including the SELECTED stage
        (or the full chain when nothing is selected), so clicking a stage
        previews 1..N. Lets the user keep adding/tuning filters on the last
        captured image.
        """
        if self.last_raw is None:
            return
        sel = self.selected_idx
        upto = sel if 0 <= sel < len(self.pipeline) else None
        display, hist_data = apply_pipeline(self.last_raw, self.pipeline, sel, upto=upto)
        if display is None:
            return
        self._show_frame(frame_to_qimage(display))
        if hist_data and 0 <= self.selected_idx < len(self.pipeline) and self.pipeline[self.selected_idx]["type"] == "binary":
            hist, thresh = hist_data
            if hasattr(self.panel, "hist"):
                self.panel.hist.set_data(hist, thresh)

    def _show_frame(self, qimg: QImage):
        """Display a frame and refresh the size readout (height × width)."""
        self.video.set_frame(qimg)
        if qimg.isNull():
            self.size_label.setText("—")
        else:
            self.size_label.setText(f"{qimg.height()} × {qimg.width()}")

    def _toggle_stream(self):
        if self.streaming:
            self._stop_stream()
        else:
            self._start_stream()

    def _start_stream(self):
        if not self.cameras or self.current_cam is None or self.current_cam < 0:
            QMessageBox.warning(self, "opencam", "No camera selected.")
            return

        self.streaming = True
        self._update_toggle_style()
        self.status_lbl.setText("Streaming...")

        self.worker = CameraWorker(self.current_cam, self.pipeline, self.selected_idx)
        self.thread = QThread()
        self.worker.moveToThread(self.thread)

        self.thread.started.connect(self.worker.run)
        self.worker.frame_ready.connect(self._on_frame)
        self.worker.fps_ready.connect(self._on_fps)
        self.worker.error.connect(self._on_error)
        self.worker.finished.connect(self._on_worker_finished)

        self.thread.start()
        # initial update already passed via ctor; safe to call again
        self.worker.update_settings(self.current_cam, self.pipeline, self.selected_idx)

    def _restart_stream(self):
        self._stop_stream()
        QTimer.singleShot(60, self._start_stream)

    def _stop_stream(self):
        self.streaming = False
        self._update_toggle_style()

        if self.worker:
            self.worker.stop()
        if self.thread:
            self.thread.quit()
            self.thread.wait(1500)
            try:
                self.thread.deleteLater()
            except Exception:
                pass
            self.thread = None
        if self.worker:
            # Freeze the last raw frame so filters stay editable while stopped.
            # Read only after the thread finished (wait above) — no race.
            if getattr(self.worker, "last_raw", None) is not None:
                self.last_raw = self.worker.last_raw
            try:
                self.worker.deleteLater()
            except Exception:
                pass
            self.worker = None

        if self.last_raw is not None:
            self._render_still()
            self.status_lbl.setText("Stopped — still frame (filters editable)")
        else:
            self._show_frame(QImage())
            self.status_lbl.setText("Stopped")

    @pyqtSlot(QImage, object)
    def _on_frame(self, qimg: QImage, hist_data):
        self._show_frame(qimg)
        if hist_data and 0 <= self.selected_idx < len(self.pipeline) and self.pipeline[self.selected_idx]["type"] == "binary":
            hist, thresh = hist_data
            if hasattr(self.panel, "hist"):
                self.panel.hist.set_data(hist, thresh)

    @pyqtSlot(float)
    def _on_fps(self, fps: float):
        if self.streaming:
            self.status_lbl.setText(f"{fps:.1f} fps  •  {self._cam_label()}")

    def _cam_label(self) -> str:
        for item in self.cameras:
            if item[0] == self.current_cam:
                return item[1].split(":", 1)[-1].strip()[:22]
        return str(self.current_cam)

    @pyqtSlot(str)
    def _on_error(self, msg: str):
        self._stop_stream()
        self.status_lbl.setText(f"Error: {msg}")
        QMessageBox.critical(self, "opencam", msg)

    @pyqtSlot()
    def _on_worker_finished(self):
        pass

    def closeEvent(self, event):
        self._stop_stream()
        super().closeEvent(event)


def main():
    app = QApplication(sys.argv)
    font = QFont("JetBrainsMono Nerd Font")
    if font.exactMatch():
        app.setFont(font)
    else:
        app.setFont(QFont("monospace", 10))

    pal = QPalette()
    pal.setColor(QPalette.ColorRole.Window, QColor(BG))
    pal.setColor(QPalette.ColorRole.WindowText, QColor(FG))
    pal.setColor(QPalette.ColorRole.Base, QColor(DARK_BG))
    pal.setColor(QPalette.ColorRole.Text, QColor(FG))
    pal.setColor(QPalette.ColorRole.Button, QColor(LIGHTER_BG))
    pal.setColor(QPalette.ColorRole.ButtonText, QColor(FG))
    pal.setColor(QPalette.ColorRole.Highlight, QColor(ACCENT))
    pal.setColor(QPalette.ColorRole.HighlightedText, QColor(BG))
    app.setPalette(pal)

    win = MainWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
