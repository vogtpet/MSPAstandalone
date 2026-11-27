#!/usr/bin/env bash
# Run MSPA, creating/repairing the project's Python environment if needed.
# Usage: ./run_mspa.sh -i input.tif -o output.tif
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
requirements="$here/requirements.txt"

# The bundled workqueue layer is always available and avoids TBB-discovery
# warnings. Override with MSPA_NUMBA_LAYER=tbb or omp.
if [ -z "${NUMBA_THREADING_LAYER:-}" ]; then
    export NUMBA_THREADING_LAYER="${MSPA_NUMBA_LAYER:-workqueue}"
fi

deps_ok() {
    "$1" -c 'import numpy, scipy, numba, PIL' >/dev/null 2>&1
}

supported_python() {
    "$1" -c 'import sys; raise SystemExit(0 if (3, 10) <= sys.version_info[:2] <= (3, 13) else 1)' >/dev/null 2>&1
}

install_deps() {
    echo "Installing MSPA Python requirements into $1 ..." >&2
    "$1" -m pip install --upgrade pip >&2
    "$1" -m pip install -r "$requirements" >&2
}

run_with() {
    exec "$1" "$here/run_mspa.py" "${@:2}"
}

# Explicit interpreter override.
if [ -n "${MSPA_PYTHON:-}" ]; then
    if [ ! -x "$MSPA_PYTHON" ]; then
        echo "ERROR: MSPA_PYTHON is not executable: $MSPA_PYTHON" >&2
        exit 2
    fi
    if ! deps_ok "$MSPA_PYTHON"; then
        echo "ERROR: MSPA_PYTHON lacks numpy/scipy/numba/Pillow:" >&2
        echo "       $MSPA_PYTHON" >&2
        exit 2
    fi
    run_with "$MSPA_PYTHON" "$@"
fi

# Existing project environments: reuse, or install missing requirements.
for candidate in \
    "$here/.venv/bin/python" \
    "$here/venv/bin/python" \
    "$here/.venv/Scripts/python.exe" \
    "$here/venv/Scripts/python.exe"
do
    if [ -x "$candidate" ]; then
        if deps_ok "$candidate"; then
            run_with "$candidate" "$@"
        fi
        if supported_python "$candidate"; then
            install_deps "$candidate"
            if deps_ok "$candidate"; then
                run_with "$candidate" "$@"
            fi
        fi
        echo "ERROR: the existing environment is not usable:" >&2
        echo "       $candidate" >&2
        echo "Remove it and rerun this script to recreate it automatically." >&2
        exit 2
    fi
done

# A parent-level venv may have been created by an older layout.
for candidate in "$here/../.venv/bin/python"; do
    if [ -x "$candidate" ] && deps_ok "$candidate"; then
        run_with "$candidate" "$@"
    fi
done

# Fresh setup: pick a Python version supported by Numba.
python_bin=""
for candidate in python3.12 python3.11 python3.10 python3; do
    if command -v "$candidate" >/dev/null 2>&1 && supported_python "$candidate"; then
        python_bin="$(command -v "$candidate")"
        break
    fi
done
if [ -z "$python_bin" ]; then
    echo "ERROR: no Python 3.10-3.13 interpreter found." >&2
    echo "Numba does not support Python 3.14 yet. On Ubuntu install e.g.:" >&2
    echo "  sudo apt install python3.10-venv" >&2
    exit 2
fi

echo "No usable Python environment found. Creating $here/.venv with $python_bin ..." >&2
if ! "$python_bin" -m venv "$here/.venv"; then
    echo "ERROR: could not create the virtual environment." >&2
    echo "On Ubuntu you may need: sudo apt install python3-venv" >&2
    exit 2
fi

venv_python="$here/.venv/bin/python"
if [ ! -x "$venv_python" ]; then
    venv_python="$here/.venv/Scripts/python.exe"
fi
install_deps "$venv_python"
if ! deps_ok "$venv_python"; then
    echo "ERROR: dependency installation did not complete." >&2
    exit 2
fi
run_with "$venv_python" "$@"
