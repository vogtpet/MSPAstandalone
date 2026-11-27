#!/usr/bin/env python3
"""Preflight feasibility check for the pure-Python MSPA engine.

Run this *before* loading a large raster.  It reads only the image header,
detects the available CPU/RAM, and estimates whether the in-memory engine can
finish the job and how long it is likely to take.

Exit status: 0 = likely safe, 1 = not enough RAM/disk, 2 = invalid input or
unable to detect the hardware.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import math
import os
import re
import shutil
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

GiB = 1024 ** 3
TiB = 1024 ** 4
GB = 1000 ** 3
TB = 1000 ** 4

# Calibrated against the engine on 1k-8k rasters (see python/README.md).
# Peak in-memory bytes per input pixel, including all full-size working arrays.
BASELINE_BYTES = int(0.25 * GiB)
BYTES_PER_PIXEL_BASE = 26.0
BYTES_PER_PIXEL_SIZE = 2.5

# Safety margins.  The engine already needs a lot of temporary memory; these
# values deliberately err on the side of "no" for a feasibility decision.
SAFETY_FACTOR = 1.10
RESERVE_FRACTION = 0.10
FIRST_RUN_COMPILE_SECONDS = 20.0
# Phase-1 memmap mode maps the bridge-test arrays: core/connector labels
# (2 x 4 B), clipped copies (2 x 1 B), domain (1 B) and corridor (1 B).
MEMMAP_BYTES_PER_PIXEL = 12.0
MEMMAP_TIME_FACTOR = 1.5


@dataclass
class Hardware:
    cpus: int
    available_cpus: int
    total_ram_bytes: int | None
    available_ram_bytes: int | None


@dataclass
class ImageInfo:
    width: int
    height: int
    bands: int
    bits_per_sample: int
    bytes_per_sample: int
    uncompressed_bytes: int
    file_size_bytes: int
    mode: str | None


@dataclass
class DiskInfo:
    output_dir: str
    free_bytes: int
    required_bytes: int
    sufficient: bool


@dataclass
class MemmapEstimate:
    resident_ram_bytes: int
    temp_bytes: int
    disk_free_bytes: int | None
    sufficient: bool | None
    output_dir: str


@dataclass
class Estimate:
    pixels: int
    bytes_per_pixel: float
    required_ram_bytes: int
    usable_ram_bytes: int | None
    sufficient: bool | None
    shortfall_bytes: int
    estimated_seconds: float
    first_run_compile_seconds: float
    index_dtype: str
    disk: DiskInfo | None
    memmap: MemmapEstimate | None


def _read_linux_meminfo() -> tuple[int | None, int | None]:
    try:
        values = {}
        with open("/proc/meminfo", encoding="ascii") as fh:
            for line in fh:
                match = re.match(r"^(MemTotal|MemAvailable):\s+(\d+)\s+kB", line)
                if match:
                    values[match.group(1)] = int(match.group(2)) * 1024
        return values.get("MemTotal"), values.get("MemAvailable")
    except OSError:
        return None, None


def _read_macos_memory() -> tuple[int | None, int | None]:
    try:
        import psutil  # type: ignore
        vm = psutil.virtual_memory()
        return int(vm.total), int(vm.available)
    except Exception:
        pass
    try:
        total = int(subprocess.check_output(
            ["sysctl", "-n", "hw.memsize"], text=True).strip())
        vm_stat = subprocess.check_output(["vm_stat"], text=True)
        page_match = re.search(r"page size of (\d+) bytes", vm_stat)
        page_size = int(page_match.group(1)) if page_match else os.sysconf("SC_PAGE_SIZE")
        pages = 0
        for key in ("Pages free", "Pages inactive", "Pages speculative"):
            match = re.search(rf"^{key}:\s+(\d+)\.$", vm_stat, re.MULTILINE)
            if match:
                pages += int(match.group(1))
        available = pages * page_size if pages else None
        return total, available
    except Exception:
        return None, None


def _read_windows_memory() -> tuple[int | None, int | None]:
    class MEMORYSTATUSEX(ctypes.Structure):
        _fields_ = [
            ("dwLength", ctypes.c_ulong),
            ("dwMemoryLoad", ctypes.c_ulong),
            ("ullTotalPhys", ctypes.c_ulonglong),
            ("ullAvailPhys", ctypes.c_ulonglong),
            ("ullTotalPageFile", ctypes.c_ulonglong),
            ("ullAvailPageFile", ctypes.c_ulonglong),
            ("ullTotalVirtual", ctypes.c_ulonglong),
            ("ullAvailVirtual", ctypes.c_ulonglong),
            ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
        ]

    try:
        status = MEMORYSTATUSEX()
        status.dwLength = ctypes.sizeof(MEMORYSTATUSEX)
        if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
            return None, None
        return int(status.ullTotalPhys), int(status.ullAvailPhys)
    except Exception:
        return None, None


def _system_cpus() -> int:
    return max(1, os.cpu_count() or 1)


def _available_cpus() -> int:
    try:
        value = int(subprocess.check_output(
            ["nproc"], text=True, stderr=subprocess.DEVNULL).strip())
        if value > 0:
            return value
    except Exception:
        pass
    try:
        return max(1, len(os.sched_getaffinity(0)))  # type: ignore[attr-defined]
    except (AttributeError, OSError):
        return _system_cpus()


def detect_hardware(cpus: int | None = None,
                    total_ram_gb: float | None = None,
                    available_ram_gb: float | None = None,
                    available_cpus: int | None = None) -> Hardware:
    system = max(1, int(cpus)) if cpus is not None else _system_cpus()
    usable = (max(1, int(available_cpus)) if available_cpus is not None
              else (system if cpus is not None else _available_cpus()))
    if sys.platform.startswith("linux"):
        total, available = _read_linux_meminfo()
    elif sys.platform == "darwin":
        total, available = _read_macos_memory()
    elif os.name == "nt":
        total, available = _read_windows_memory()
    else:
        total, available = None, None

    if total_ram_gb is not None:
        total = int(total_ram_gb * GB)
    if available_ram_gb is not None:
        available = int(available_ram_gb * GB)
    if available is None and total is not None:
        # No MemAvailable equivalent: assume 80% is usable, never 100%.
        available = int(total * 0.8)
    return Hardware(cpus=system, available_cpus=usable,
                    total_ram_bytes=total, available_ram_bytes=available)


def _bits_for_mode(mode: str, bands: int) -> int:
    if mode == "1":
        return 1
    if mode.startswith("I;16"):
        return 16
    if mode in ("I", "F"):
        return 32
    if mode in ("L", "P", "RGB", "RGBA", "CMYK", "YCbCr", "LAB", "HSV"):
        return 8
    return 8


def inspect_image(path: Path) -> ImageInfo:
    try:
        from PIL import Image
    except ImportError as exc:  # pragma: no cover - Pillow is in requirements
        raise RuntimeError("Pillow is required to inspect the input image") from exc

    with Image.open(path) as im:
        width, height = im.size
        mode = im.mode
        bands = len(im.getbands())
        bits = _bits_for_mode(mode, bands)
        # TIFF BitsPerSample is authoritative when present.
        try:
            tag = im.tag_v2.get(258)
            if isinstance(tag, (tuple, list)):
                bits = int(tag[0])
            elif tag is not None:
                bits = int(tag)
        except Exception:
            pass
        bytes_per_sample = max(1, (bits + 7) // 8)
    uncompressed = width * height * bytes_per_sample * bands
    return ImageInfo(
        width=width,
        height=height,
        bands=bands,
        bits_per_sample=bits,
        bytes_per_sample=bytes_per_sample,
        uncompressed_bytes=uncompressed,
        file_size_bytes=os.path.getsize(path),
        mode=mode,
    )


def engine_bytes_per_pixel(edge_width: float) -> float:
    """Peak working-set bytes per pixel, calibrated from the engine."""
    size = (edge_width + 0.98) / math.sqrt(2.0)
    return BYTES_PER_PIXEL_BASE + BYTES_PER_PIXEL_SIZE * size


def estimate_time_seconds(pixels: int, edge_width: float, cpus: int) -> float:
    """Rough steady-state runtime estimate (first-run JIT excluded)."""
    size = (edge_width + 0.98) / math.sqrt(2.0)
    seconds_per_megapixel = 0.60
    # Only part of the pipeline scales with threads; assume that part to be
    # 40% and cap the useful thread count at 16.  This matches the measured
    # limited OpenMP/thread scaling of the serial MSPA stages.
    parallel_fraction = 0.40
    factor = (1.0 - parallel_fraction) + parallel_fraction * 16.0 / cpus
    return (pixels / 1e6) * seconds_per_megapixel * factor


def build_estimate(info: ImageInfo, hardware: Hardware, edge_width: float,
                   output_dir: Path | None = None,
                   memmap_dir: Path | None = None,
                   memmap_free_bytes: int | None = None) -> Estimate:
    pixels = info.width * info.height
    bpp = engine_bytes_per_pixel(edge_width)
    if pixels > 2 ** 31:
        # 64-bit queues are only needed above 2^31 pixels.
        bpp += 1.5
    required = int((BASELINE_BYTES + bpp * pixels) * SAFETY_FACTOR)
    usable = None
    sufficient = None
    shortfall = 0
    if hardware.available_ram_bytes is not None:
        usable = int(hardware.available_ram_bytes * (1.0 - RESERVE_FRACTION))
        sufficient = required <= usable
        shortfall = max(0, required - usable)

    disk = None
    if output_dir is not None:
        output_dir.mkdir(parents=True, exist_ok=True)
        free = shutil.disk_usage(output_dir).free
        # The output is a single uint8 band (plus TIFF overhead).
        required_disk = int(pixels * 1.05)
        disk = DiskInfo(str(output_dir), free, required_disk,
                        free >= required_disk)

    memmap = None
    if memmap_dir is not None:
        memmap_dir.mkdir(parents=True, exist_ok=True)
        resident = int((BASELINE_BYTES
                        + max(0.0, bpp - MEMMAP_BYTES_PER_PIXEL) * pixels)
                       * SAFETY_FACTOR)
        temp = int(MEMMAP_BYTES_PER_PIXEL * pixels * 1.05) + int(pixels * 1.05)
        free = (memmap_free_bytes if memmap_free_bytes is not None
                else shutil.disk_usage(memmap_dir).free)
        ok = (usable is not None and resident <= usable and free >= temp)
        memmap = MemmapEstimate(resident, temp, free, ok, str(memmap_dir))

    return Estimate(
        pixels=pixels,
        bytes_per_pixel=round(bpp, 2),
        required_ram_bytes=required,
        usable_ram_bytes=usable,
        sufficient=sufficient,
        shortfall_bytes=shortfall,
        estimated_seconds=estimate_time_seconds(
            pixels, edge_width, hardware.available_cpus),
        first_run_compile_seconds=FIRST_RUN_COMPILE_SECONDS,
        index_dtype="int64" if pixels > 2 ** 31 else "int32",
        disk=disk,
        memmap=memmap,
    )


def format_bytes(value: int | None) -> str:
    if value is None:
        return "unknown"
    if value >= TiB:
        return f"{value / TiB:.2f} TiB ({value / TB:.2f} TB)"
    if value >= GiB:
        return f"{value / GiB:.1f} GiB ({value / GB:.1f} GB)"
    if value >= 1024 ** 2:
        return f"{value / 1024 ** 2:.1f} MiB"
    return f"{value} bytes"


def format_duration(seconds: float) -> str:
    if seconds < 90:
        return f"~{seconds:.0f} s"
    if seconds < 3600:
        return f"~{seconds / 60:.1f} min"
    if seconds < 86400:
        return f"~{seconds / 3600:.1f} h"
    return f"~{seconds / 86400:.1f} days"


def print_report(info: ImageInfo, hardware: Hardware, estimate: Estimate,
                 edge_width: float) -> None:
    print("MSPA Python preflight")
    print("=====================")
    print(f"Input:              {info.width} x {info.height}, "
          f"{info.bands} band(s), {info.bits_per_sample}-bit, mode {info.mode}")
    print(f"Uncompressed size:  {format_bytes(info.uncompressed_bytes)}")
    print(f"File size:          {format_bytes(info.file_size_bytes)}")
    print()
    print(f"CPUs:               {hardware.cpus} logical "
          f"(available: {hardware.available_cpus})")
    print(f"RAM total:          {format_bytes(hardware.total_ram_bytes)}")
    print(f"RAM available:      {format_bytes(hardware.available_ram_bytes)}")
    print(f"Reserve:            {RESERVE_FRACTION * 100:.0f}% of available RAM")
    print()
    print(f"Edge width:         {edge_width}")
    print(f"Index queues:       {estimate.index_dtype}")
    print(f"Model:              {estimate.bytes_per_pixel:.1f} bytes/pixel "
          f"(+ {format_bytes(BASELINE_BYTES)} baseline, "
          f"{SAFETY_FACTOR:.2f}x safety)")
    print(f"Estimated peak RAM: {format_bytes(estimate.required_ram_bytes)}")
    if estimate.usable_ram_bytes is not None:
        print(f"Usable RAM:         {format_bytes(estimate.usable_ram_bytes)}")
    if estimate.sufficient is None:
        print("RAM verdict:        UNKNOWN (could not detect available RAM)")
    elif estimate.sufficient:
        print("RAM verdict:        YES")
    else:
        print("RAM verdict:        NO")
        print(f"Shortfall:          {format_bytes(estimate.shortfall_bytes)}")
    print(f"Estimated time:     {format_duration(estimate.estimated_seconds)} "
          f"steady state, plus ~{estimate.first_run_compile_seconds:.0f} s "
          f"first-run Numba compile")
    if estimate.disk is not None:
        d = estimate.disk
        print(f"Output directory:   {d.output_dir}")
        print(f"Output space need:  {format_bytes(d.required_bytes)}")
        print(f"Free disk:          {format_bytes(d.free_bytes)}")
        print(f"Disk verdict:       {'YES' if d.sufficient else 'NO'}")
    if estimate.memmap is not None:
        m = estimate.memmap
        print()
        print("Memory-mapped mode:")
        print(f"Temp directory:     {m.output_dir}")
        print(f"Resident RAM:       {format_bytes(m.resident_ram_bytes)}")
        print(f"Temp + output disk: {format_bytes(m.temp_bytes)}")
        print(f"Free disk:          {format_bytes(m.disk_free_bytes)}")
        print(f"Memmap verdict:     {'YES' if m.sufficient else 'NO'}")
    print()
    ram_ok = estimate.sufficient is True
    memmap_ok = estimate.memmap is not None and estimate.memmap.sufficient is True
    disk_ok = estimate.disk is None or estimate.disk.sufficient
    ok = (ram_ok and disk_ok) or (memmap_ok and disk_ok)
    mode = "RAM" if ram_ok else ("MEMMAP" if memmap_ok else "")
    print("VERDICT:            " + ("PROCEED (" + mode + ")" if ok else "DO NOT PROCEED"))
    if not ram_ok and memmap_ok:
        print("The in-memory engine does not fit, but memory-mapped mode does.")
        print(f"Use memmap_dir='{estimate.memmap.output_dir}' with "
              f"memmap_min_bytes=67108864 (64 MiB).")
        print(f"Expect roughly {MEMMAP_TIME_FACTOR:.1f}x the steady-state time.")
    elif not ok:
        if estimate.sufficient is False:
            print("The in-memory engine does not fit in the usable RAM.")
        if estimate.memmap is None:
            print("Pass --memmap-dir to evaluate disk-backed processing.")
        elif not estimate.memmap.sufficient:
            print("Memory-mapped mode needs more free temp disk or RAM.")
        print("Use the C binary, a larger temp disk, or tiled processing.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("input", nargs="?", type=Path,
                        help="input raster (only the header is read)")
    parser.add_argument("--width", type=int,
                        help="simulate a map width without an input file")
    parser.add_argument("--height", type=int,
                        help="simulate a map height without an input file")
    parser.add_argument("--bytes-per-sample", type=int, default=1,
                        help="bytes per sample for --width/--height (default: 1)")
    parser.add_argument("--edge-width", type=float, default=1.0,
                        help="MSPA edge width / -eew (default: 1.0)")
    parser.add_argument("--cpus", type=int,
                        help="override the detected logical CPU count")
    parser.add_argument("--available-cpus", type=int,
                        help="override the usable CPU count (nproc)")
    parser.add_argument("--total-ram-gb", type=float,
                        help="override total RAM in decimal GB")
    parser.add_argument("--available-ram-gb", type=float,
                        help="override available RAM in decimal GB")
    parser.add_argument("--output-dir", type=Path, default=None,
                        help="also check free space for the output TIFF")
    parser.add_argument("--memmap-dir", type=Path, default=None,
                        help="evaluate memory-mapped mode and check temp disk space")
    parser.add_argument("--memmap-free-gb", type=float, default=None,
                        help="override free temp-disk space in decimal GB")
    parser.add_argument("--json", action="store_true",
                        help="print a machine-readable JSON report")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.input is None and (args.width is None or args.height is None):
        print("ERROR: provide an input file or both --width and --height.",
              file=sys.stderr)
        return 2

    if args.input is not None:
        if not args.input.is_file():
            print(f"ERROR: input not found: {args.input}", file=sys.stderr)
            return 2
        try:
            info = inspect_image(args.input)
        except Exception as exc:
            print(f"ERROR: cannot inspect {args.input}: {exc}", file=sys.stderr)
            return 2
    else:
        assert args.width is not None and args.height is not None
        bits = args.bytes_per_sample * 8
        info = ImageInfo(
            width=args.width,
            height=args.height,
            bands=1,
            bits_per_sample=bits,
            bytes_per_sample=args.bytes_per_sample,
            uncompressed_bytes=args.width * args.height * args.bytes_per_sample,
            file_size_bytes=0,
            mode="simulated",
        )

    if info.bands != 1:
        print("ERROR: MSPA expects a single-band input image; "
              f"this image has {info.bands} bands.", file=sys.stderr)
        return 2

    hardware = detect_hardware(args.cpus, args.total_ram_gb,
                               args.available_ram_gb, args.available_cpus)
    estimate = build_estimate(
        info, hardware, args.edge_width, args.output_dir, args.memmap_dir,
        int(args.memmap_free_gb * GB) if args.memmap_free_gb is not None else None,
    )

    if args.json:
        print(json.dumps({
            "input": asdict(info),
            "hardware": asdict(hardware),
            "estimate": asdict(estimate),
            "verdict": (
                "proceed_ram" if estimate.sufficient
                else "proceed_memmap" if (estimate.memmap and estimate.memmap.sufficient)
                else "do_not_proceed"),
        }, indent=2))
    else:
        print_report(info, hardware, estimate, args.edge_width)

    if estimate.sufficient is None:
        return 2
    ram_ok = estimate.sufficient is True
    memmap_ok = estimate.memmap is not None and estimate.memmap.sufficient is True
    disk_ok = estimate.disk is None or estimate.disk.sufficient
    ok = (ram_ok and disk_ok) or (memmap_ok and disk_ok)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
