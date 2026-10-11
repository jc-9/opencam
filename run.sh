#!/usr/bin/env bash
# opencam — USB webcam + C-mount lens setup tool
# Single-instance guard + focus existing window on relaunch (Hyprland Lua aware)
set -euo pipefail
cd "$(dirname "$0")"

# Interpreter selection: install.sh choice, then venv, then system python.
PY=""
if [ -f ./.python-path ]; then
  PY="$(cat ./.python-path)"
  if { [ -x "$PY" ] || command -v "$PY" >/dev/null 2>&1; } \
     && "$PY" -c "import PyQt6.QtWidgets, cv2, numpy" >/dev/null 2>&1; then
    :
  else
    PY=""
  fi
fi
if [ -z "$PY" ]; then
  if [ -x ./venv/bin/python ] \
     && ./venv/bin/python -c "import PyQt6.QtWidgets, cv2, numpy" >/dev/null 2>&1; then
    PY="./venv/bin/python"
  elif python3 -c "import PyQt6.QtWidgets, cv2, numpy" >/dev/null 2>&1; then
    PY="python3"
  else
    echo "No working Python found — run ./install.sh first." >&2
    exit 1
  fi
fi

LOCK=/tmp/opencam.lock
exec 9>"$LOCK"
if ! flock -n 9; then
  # Focus any existing opencam window
  hyprctl eval '
    for _,w in ipairs(hl.get_windows()) do
      if w.title and w.title:find("opencam") then
        hl.dispatch(hl.dsp.focus({ window = w }))
        break
      end
    end
  ' 2>/dev/null || true
  notify-send "opencam" "Already running — focused the window." 2>/dev/null || true
  exit 0
fi

exec "$PY" app.py "$@"
