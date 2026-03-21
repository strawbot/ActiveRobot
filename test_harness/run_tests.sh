#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# run_tests.sh  –  Set up and run the device test harness on macOS
#
# Usage:
#   cd <this directory>
#   chmod +x run_tests.sh
#   ./run_tests.sh
#
# What it does:
#   1. Creates a Python virtual environment (venv) if one doesn't exist.
#   2. Installs dependencies into the venv.
#   3. Runs the full pytest suite.
#   4. Saves a debug log to test_results.log.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

VENV_DIR="$SCRIPT_DIR/.venv"

# ── 1. Create venv if needed ──────────────────────────────────────────────────
if [ ! -d "$VENV_DIR" ]; then
    echo "Creating Python virtual environment..."
    python3 -m venv "$VENV_DIR"
fi

# ── 2. Activate venv ─────────────────────────────────────────────────────────
# shellcheck disable=SC1091
source "$VENV_DIR/bin/activate"

# ── 3. Install / upgrade dependencies ────────────────────────────────────────
echo "Installing dependencies..."
pip install --quiet --upgrade pip
pip install --quiet -r requirements.txt

# ── 4. Show detected serial ports (helpful for debugging config) ──────────────
echo ""
echo "Serial ports visible on this machine:"
ls /dev/tty.usb* /dev/tty.serial* 2>/dev/null || echo "  (none found – check USB connections)"
echo ""

# ── 5. Run the test suite ─────────────────────────────────────────────────────
echo "Running test suite..."
echo ""
python3 -m pytest "$@"

# ── 6. Remind about the log file ─────────────────────────────────────────────
echo ""
echo "Full debug log saved to: test_results.log"
