#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
python3 -m venv "$root/.venv"
"$root/.venv/bin/python" -m pip install --upgrade pip
"$root/.venv/bin/python" -m pip install -r "$root/CrossPlatform/requirements.txt"
"$root/scripts/build-native.sh"
"$root/scripts/build-server.sh"

echo
echo "Hyrule Together is ready. Start the GUI with:"
echo "  $root/scripts/run-gui.sh"
