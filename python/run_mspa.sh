#!/usr/bin/env bash
# Run the MSPA Python CLI with the project's virtual environment.
# Usage: ./run_mspa.sh -i input.tif -o output.tif
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Optional explicit override.
if [ -n "${MSPA_PYTHON:-}" ] && [ -x "${MSPA_PYTHON}" ]; then
    exec "${MSPA_PYTHON}" "$here/run_mspa.py" "$@"
fi

for candidate in \
    "$here/.venv/bin/python" \
    "$here/venv/bin/python" \
    "$here/.venv/Scripts/python.exe" \
    "$here/venv/Scripts/python.exe" \
    "$here/../.venv/bin/python"
do
    if [ -x "$candidate" ]; then
        exec "$candidate" "$here/run_mspa.py" "$@"
    fi
done

# Last resort: a system Python that already has the dependencies.
for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 \
       && "$candidate" -c 'import numpy, scipy, numba, PIL' >/dev/null 2>&1; then
        exec "$candidate" "$here/run_mspa.py" "$@"
    fi
done

cat >&2 <<'EOF'
ERROR: no Python environment with numpy/scipy/numba/Pillow was found.

Create it once in this directory:
  python3 -m venv .venv
  . .venv/bin/activate
  pip install -r requirements.txt

Then run:
  ./run_mspa.sh -i input.tif -o output.tif
EOF
exit 2
