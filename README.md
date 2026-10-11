# opencam — v0.1

Beautiful, clean Omarchy-style tool for setting up USB webcams with manual C-mount lenses.

Live preview of the camera feed with a stackable, numbered filter pipeline to help visually tune focus, alignment and aperture using the mechanical lens ring.

Stopping the stream freezes the last frame — filters stay fully editable on the still image (add, reorder, remove, tune params), so you can keep working without the camera running.

## Requirements

- Linux with V4L2 cameras (`/dev/video*`; `v4l2-ctl` from `v4l-utils` recommended for USB camera names)
- Python 3.10+
- PyQt6, OpenCV, NumPy (see `requirements.txt`)

## Install from source

```bash
git clone https://github.com/jc-9/opencam.git
cd opencam
python3 -m venv venv
./venv/bin/pip install -r requirements.txt
./run.sh
```

(`run.sh` uses `./venv/bin/python app.py` with a single-instance guard; if your
distro ships PyQt6 system-wide you can create the venv with `--system-site-packages`
instead.)

## Controls (all inside the single window)

Menus live in a left sidebar (vertical stack, ready for more dropdowns):

- **▶ START STREAM / ⏹ STOP STREAM** — toggle (full-width in sidebar)
- **Camera** dropdown — auto-detected V4L2 (USB first). Shows **Path: /dev/videoX** explicitly below it. Robust autodetection via v4l2-ctl + fallback.
- **Add Filter** dropdown + **Add** button — appends a filter to the pipeline
- **Applied Filters** numbered list — "1. Gaussian Smoothing", "2. Binary Threshold", ... Select an entry to edit it in the right panel; **✕ / ↑ / ↓** buttons remove / reorder it.
- **Numbered pipeline label** above the preview — "1:Gaussian → [2:Binary] → 3:Sobel" shows the applied order on screen; brackets mark the viewed stage.
- Clicking a stage previews the **cumulative result up to that stage**: click 3 → effects of 1→2→3, click 2 → 1→2, click 1 → only 1. Works live and on the frozen still frame.
- Filters apply sequentially (each stage's output feeds the next). The histogram shows the input of the selected binary stage.
- Dynamic right panel (follows same layout convention; restores saved params when you reselect a stage):
  - Binary Threshold: threshold input field + mini live histogram with draggable red threshold line
  - Gaussian Smoothing: Kernel Size + Number of passes (1-5)
  - Sobel X: kernel size controls (3/5/7)
  - Image Color: Mode (Color / Greyscale)
  - Morphological: operation, kernel_shape, kernel_size (odd 3-21), iterations
  - Canny: thresh1/2, aperture, L2gradient
  - Median Blur: kernel_size (odd 3-15)
  - Bilateral Filter: d, sigma_color, sigma_space
  - Color Threshold: color_space (HSV/LAB/YCrCb), lower/upper per channel, invert

Fully compatible with current Omarchy theme (loads colors.toml at runtime, applies to palette + QSS). Switch themes then restart app to pick up new colors.

## Run

```bash
./run.sh
```

Or directly:

```bash
./venv/bin/python app.py
```

## Install to app menu (Omarchy / Hyprland)

```bash
mkdir -p ~/.local/share/applications
cp opencam.desktop ~/.local/share/applications/
update-desktop-database ~/.local/share/applications
```

(Icon uses the standard "camera-web" — works in most icon themes.)

## Stack

- PyQt6 (styled to match Omarchy theme palettes via colors.toml)
- opencv-python-headless + numpy
- Single live view + right dynamic params panel
- Left sidebar for controls (expandable for future dropdowns)
- Explicit camera device path display

## Verification

- Camera enumeration + path
- Theme colors loaded from active Omarchy theme
- Live capture + filter switching (binary with interactive hist, sobel)
- Multi-filter pipeline: add/select/reorder/remove stages; numbered list + on-screen pipeline label; sequential worker chain; param restore on reselect (18-check offscreen test, includes prior-crash repro scenario)
- Sidebar + clean layout, no overlaps
- CIFS safe
