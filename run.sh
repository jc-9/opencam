#!/usr/bin/env bash
# opencam — USB webcam + C-mount lens setup tool
# Single-instance guard + focus existing window on relaunch (Hyprland Lua aware)
set -euo pipefail
cd "$(dirname "$0")"

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

exec ./venv/bin/python app.py "$@"
