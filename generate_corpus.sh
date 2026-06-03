#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

VENV_DIR="${VENV_DIR:-.venv}"
PYTHON_BIN="$VENV_DIR/bin/python"

# Keep corpus generation pinned to the project venv so NumPy/Python versions are
# the same when another agent or shell session reruns the benchmark setup.
if [[ ! -x "$PYTHON_BIN" ]]; then
  echo "Missing virtual environment at $VENV_DIR"
  echo "Run: ./setup_venv.sh"
  exit 1
fi

# The Python generator is deterministic; rerunning this should reproduce the
# same files and checksums unless generator logic or dependencies changed.
"$PYTHON_BIN" generate_he_corpus.py
