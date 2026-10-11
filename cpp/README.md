# opencam — C++ port (Qt6 Widgets + OpenCV 5)

Same app as `../app.py`, rewritten in C++ for lower memory use, no
interpreter/GIL overhead, and native threading. Feature parity: numbered
filter pipeline, cumulative 1..N preview, still-frame editing, histogram,
Omarchy theme, V4L2 enumeration via ioctl.

## Build

Needs Qt6 Widgets dev files, OpenCV 5 C++ headers, and `qmake6`
(all present on Omarchy; no sudo required):

```bash
cd cpp
./build.sh        # qmake6 + make → build/opencam
```

## Run

```bash
cd cpp
./build/opencam
QT_QPA_PLATFORM=offscreen ./build/opencam   # headless smoke
```

## Self-test + benchmark (no camera needed)

```bash
./build/opencam --self-test   # asserts + full 9-filter 720p chain timing
```

Measured on this machine (1280x720, full chain incl. Rotate, 20 iterations):

| implementation | chain time | idle RSS |
|---|---|---|
| Python (PyQt6 + OpenCV) | ~50 ms | ~119 MB |
| C++ (Qt6 + OpenCV) | ~51 ms | ~99 MB |

(Absolute ms varies with machine load; measured back-to-back the two stay
at parity.) The chain itself is OpenCV-bound (identical kernels either way),
so frame time is at parity; the C++ binary wins on memory (~17% leaner),
startup, and freedom from GIL stalls. Rotate at 0° is a zero-cost
passthrough in both. Further headroom (not done): GPU UMats, threaded
stages.

## Switching the app launcher to the C++ build

The installed `~/.local/share/applications/opencam.desktop` still points at
the Python `run.sh`. To switch after verifying the C++ build on your camera:

```bash
# edit Exec= to: /home/justin/Projects/COMPUTER_VISION/opencam_101126/cpp/build/opencam
```

## Layout

- `opencam.pro` — qmake project (no cmake needed)
- `src/filters.*` — pure-OpenCV chain (`applyPipeline`), no Qt Widgets dep
- `src/worker.*` — QThread capture worker
- `src/mainwindow.*`, `src/filterpanel.*` — UI
- `src/histogram.*`, `src/videodisplay.*` — custom widgets
- `src/theme.*` — Omarchy `colors.toml` palette loader
- `src/cameras.*` — V4L2 enumeration via ioctl
