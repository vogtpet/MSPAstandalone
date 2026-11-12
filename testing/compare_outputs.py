#!/usr/bin/env python3
"""Compare the pixel data of two MSPA output images (requires Pillow).

Usage: python3 compare_outputs.py reference.tif candidate.tif

Exit status: 0 if the pixel data is identical, 1 otherwise.
"""

import sys
from collections import Counter

try:
    from PIL import Image
except ImportError:
    sys.exit("This script requires Pillow: python3 -m pip install Pillow")


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip())
        return 2

    ref_path, cand_path = sys.argv[1], sys.argv[2]
    ref = Image.open(ref_path)
    cand = Image.open(cand_path)

    if ref.size != cand.size:
        print(f"FAIL: size differs: {ref.size} vs {cand.size}")
        return 1
    if ref.mode != cand.mode:
        print(f"FAIL: pixel mode differs: {ref.mode} vs {cand.mode}")
        return 1

    # tobytes() returns the raw pixel values; for MSPA output (palette
    # images) these are exactly the MSPA class codes.
    ref_px = ref.tobytes()
    cand_px = cand.tobytes()
    if ref_px == cand_px:
        print(f"OK: {ref.size[0]}x{ref.size[1]} pixels are identical")
        return 0

    diff = Counter((a, b) for a, b in zip(ref_px, cand_px) if a != b)
    print(f"FAIL: {sum(diff.values())} pixels differ")
    for (a, b), n in diff.most_common(10):
        print(f"  reference {a:3d} -> candidate {b:3d}: {n} pixels")
    return 1


if __name__ == "__main__":
    sys.exit(main())
