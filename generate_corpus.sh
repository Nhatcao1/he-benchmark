#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

VENV_DIR="${VENV_DIR:-.venv}"
PYTHON_BIN="$VENV_DIR/bin/python"

if [[ ! -x "$PYTHON_BIN" ]]; then
  echo "Missing virtual environment at $VENV_DIR"
  echo "Run: ./setup_venv.sh"
  exit 1
fi

"$PYTHON_BIN" generate_he_corpus.py
