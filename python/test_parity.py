#!/usr/bin/env python3
"""Pixel-for-pixel parity test of the pure-Python MSPA engine.

Runs the engine on ``source/testing/input.tif`` and compares the result with
the reference images produced by the MSPA C code.  Exit status is non-zero if
any pixel differs.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

from mspa import segment_binary_patterns

ROOT = Path(__file__).resolve().parents[1]
TESTING = ROOT / "source" / "testing"

# (label, engine keyword arguments, reference image)
CASES = [
    (
        "default (edge width 1.0, 8-connectivity, internal, transition)",
        dict(edge_width=1.0, graphfg=8, transition=1, internal=1),
        "reference.tif",
    ),
    (
        "edge width 1.0, 4-connectivity, no internal classes",
        dict(edge_width=1.0, graphfg=4, transition=1, internal=0),
        "reference_nondefault.tif",
    ),
    (
        "edge width 5.0, 8-connectivity, internal classes",
        dict(edge_width=5.0, graphfg=8, transition=1, internal=1),
        "reference_eew5.tif",
    ),
    (
        "edge width 5.0, 4-connectivity, no internal classes",
        dict(edge_width=5.0, graphfg=4, transition=1, internal=0),
        "reference_eew5_nondefault.tif",
    ),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        type=Path,
        default=TESTING / "input.tif",
        help="input image (0=nodata, 1=background, 2=foreground)",
    )
    parser.add_argument(
        "--reference-dir",
        type=Path,
        default=TESTING,
        help="directory containing reference.tif and reference_nondefault.tif",
    )
    parser.add_argument(
        "--n-jobs",
        type=int,
        default=1,
        help="worker threads for the independent MSPA stages (default: 1)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.input.is_file():
        print(f"ERROR: input image not found: {args.input}", file=sys.stderr)
        return 2

    imin = np.asarray(Image.open(args.input))
    if imin.ndim != 2:
        print(f"ERROR: expected a single-band image, got shape {imin.shape}", file=sys.stderr)
        return 2

    failures = 0
    for label, kwargs, reference_name in CASES:
        reference_path = args.reference_dir / reference_name
        if not reference_path.is_file():
            print(f"SKIP: reference image not found: {reference_path}")
            failures += 1
            continue

        reference = np.asarray(Image.open(reference_path))
        started = time.perf_counter()
        result = segment_binary_patterns(imin, n_jobs=args.n_jobs, **kwargs)
        elapsed = time.perf_counter() - started

        if result.shape != reference.shape:
            print(f"FAIL: {label}: shape {result.shape} != {reference.shape}")
            failures += 1
            continue

        different = result != reference
        count = int(np.count_nonzero(different))
        if count:
            failures += 1
            ys, xs = np.nonzero(different)
            print(f"FAIL: {label}: {count} differing pixel(s)")
            for y, x in list(zip(ys.tolist(), xs.tolist()))[:10]:
                print(
                    f"      ({y}, {x}): reference={int(reference[y, x])} "
                    f"python={int(result[y, x])}"
                )
        else:
            print(f"OK:   {label}: {different.size} pixels identical ({elapsed:.2f} s)")

    if failures:
        print(f"\n{failures} parity case(s) failed.")
        return 1

    print("\nAll parity cases passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
