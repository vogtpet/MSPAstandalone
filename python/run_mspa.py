#!/usr/bin/env python3
"""Run MSPA on a single raster from the command line.

Examples:
    python run_mspa.py -i input.tif -o output.tif
    python run_mspa.py -i huge.tif -o out.tif --memmap-dir /scratch/mspa-temp
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import numpy as np
from PIL import Image

from mspa import segment_binary_patterns
from mspa.palettes import mspa_palette
import preflight


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Run MSPA with the pure-Python engine.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("-i", "--input", required=True, type=Path,
                   help="input image (0=nodata, 1=background, 2=foreground)")
    p.add_argument("-o", "--output", default="out.tif", type=Path,
                   help="output image name (default: out.tif)")
    p.add_argument("-odir", "--output-dir", type=Path, default=Path("."),
                   help="output directory for a relative -o (default: .)")
    p.add_argument("-eew", "--edge-width", type=float, default=1.0,
                   help="effective edge width (default: 1.0)")
    p.add_argument("-graphfg", "--graphfg", type=int, choices=(4, 8), default=8,
                   help="foreground connectivity (default: 8)")
    p.add_argument("-internal", "--internal", type=int, choices=(0, 1), default=1,
                   help="include internal classes (default: 1)")
    p.add_argument("-transition", "--transition", type=int, choices=(0, 1), default=1,
                   help="transition palette flag (default: 1)")
    p.add_argument("-j", "--jobs", type=int, default=None,
                   help="threads (default: all available cores)")
    p.add_argument("--memmap-dir", type=Path, default=None,
                   help="temporary directory for memory-mapped processing")
    p.add_argument("--memmap-min-mb", type=float, default=64.0,
                   help="map arrays at least this large (default: 64 MiB)")
    p.add_argument("--skip-preflight", action="store_true",
                   help="do not check RAM/disk before starting")
    p.add_argument("--force", action="store_true",
                   help="run even if the preflight says it may not fit")
    return p.parse_args()


def output_path(args: argparse.Namespace) -> Path:
    if args.output.is_absolute():
        return args.output
    args.output_dir.mkdir(parents=True, exist_ok=True)
    return args.output_dir / args.output


def main() -> int:
    args = parse_args()
    if not args.input.is_file():
        print(f"ERROR: input not found: {args.input}", file=sys.stderr)
        return 2

    out_path = output_path(args)

    if not args.skip_preflight:
        try:
            info = preflight.inspect_image(args.input)
            hardware = preflight.detect_hardware()
            estimate = preflight.build_estimate(
                info, hardware, args.edge_width, out_path.parent, args.memmap_dir
            )
            preflight.print_report(info, hardware, estimate, args.edge_width)
            ram_ok = estimate.sufficient is True
            memmap_ok = estimate.memmap is not None and estimate.memmap.sufficient is True
            disk_ok = estimate.disk is None or estimate.disk.sufficient
            if not ((ram_ok or memmap_ok) and disk_ok) and not args.force:
                print("\nERROR: preflight says this job may not fit. "
                      "Use --force to override.", file=sys.stderr)
                return 1
        except Exception as exc:
            if not args.force:
                print(f"ERROR: preflight failed: {exc}", file=sys.stderr)
                return 2
            print(f"WARNING: preflight failed ({exc}); continuing because of --force.",
                  file=sys.stderr)

    with Image.open(args.input) as im:
        bands = len(im.getbands())
        if bands != 1:
            print(f"ERROR: expected a single-band image, got {bands} bands.",
                  file=sys.stderr)
            return 2
        imin = np.asarray(im)

    if imin.ndim != 2:
        print(f"ERROR: expected a 2-D image, got shape {imin.shape}.", file=sys.stderr)
        return 2
    if imin.size and int(imin.max()) < 2:
        print("WARNING: the input has no value 2. MSPA expects 1=background and "
              "2=foreground; this image will be treated as all background.",
              file=sys.stderr)

    print(f"\nRunning MSPA ({imin.shape[1]} x {imin.shape[0]}) ...", flush=True)
    out = segment_binary_patterns(
        imin,
        edge_width=args.edge_width,
        graphfg=args.graphfg,
        transition=args.transition,
        internal=args.internal,
        n_jobs=args.jobs,
        memmap_dir=str(args.memmap_dir) if args.memmap_dir is not None else None,
        memmap_min_bytes=max(0, int(args.memmap_min_mb * 1024 * 1024)),
    )

    image = Image.fromarray(np.ascontiguousarray(out, dtype=np.uint8), mode="P")
    image.putpalette(mspa_palette(args.transition))
    image.save(out_path, compression="tiff_lzw")
    print(f"Wrote {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
