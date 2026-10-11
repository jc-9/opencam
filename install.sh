#!/usr/bin/env bash
# opencam installer — no sudo, idempotent, safe to re-run.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

python_import_check() {
  "$1" -c "import PyQt6.QtWidgets, cv2, numpy" >/dev/null 2>&1
}

echo "[1/5] Choosing Python interpreter..."
PYTHON_PATH=""
if [ -f ./.python-path ]; then
  PYTHON_PATH="$(cat ./.python-path)"
  python_import_check "$PYTHON_PATH" || { rm -f ./.python-path; PYTHON_PATH=""; }
fi
if [ -z "$PYTHON_PATH" ]; then
  if [ -x ./venv/bin/python ] && python_import_check ./venv/bin/python; then
    PYTHON_PATH="./venv/bin/python"
  elif command -v python3 >/dev/null 2>&1 && python_import_check python3; then
    PYTHON_PATH="python3"
  else
    echo "[1/5] No working interpreter found — creating venv..."
    rm -rf venv
    python3 -m venv --system-site-packages venv
    ./venv/bin/pip install --quiet -r requirements.txt
    python_import_check ./venv/bin/python || {
      echo "ERROR: dependencies unavailable. Install PyQt6, OpenCV and NumPy" >&2
      echo "(Arch: python-pyqt6 python-opencv python-numpy), then re-run ./install.sh" >&2
      exit 1
    }
    PYTHON_PATH="./venv/bin/python"
  fi
fi
echo "$PYTHON_PATH" > ./.python-path

echo "[2/5] Generating desktop entry..."
mkdir -p ~/.local/share/applications
sed "s|@INSTALL_DIR@|$SCRIPT_DIR|" < opencam.desktop.template \
  > ~/.local/share/applications/opencam.desktop

echo "[3/5] Installing icon..."
mkdir -p ~/.local/share/icons/hicolor/scalable/apps
cp opencam.svg ~/.local/share/icons/hicolor/scalable/apps/

echo "[4/5] Refreshing caches..."
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database ~/.local/share/applications
fi
if [ -d ~/.local/share/icons/hicolor ] && command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -q -t ~/.local/share/icons/hicolor || true
fi

echo "[5/5] Final self-check..."
if python_import_check "$PYTHON_PATH"; then
  echo "Installed. Search 'opencam' in your app launcher."
else
  echo "ERROR: final self-check failed." >&2
  exit 1
fi
