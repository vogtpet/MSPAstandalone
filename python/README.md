# Pure-Python MSPA

This directory contains an alternative, pure-Python implementation of MSPA.
It is **not** a replacement for the C implementation in `source/`: the C build
remains the reference and the source of the published binaries.  The Python
engine exists so that MSPA can also be used where a compiled executable cannot
be shipped, for example in a future QGIS plugin.

## Status

The engine reproduces the C implementation **pixel for pixel** on the
repository test image:

| Run | Parameters | Result |
|---|---|---|
| `reference.tif` | edge width 1.0, 8-connectivity, internal, transition | 0 differing pixels |
| `reference_nondefault.tif` | edge width 1.0, 4-connectivity, no internal classes | 0 differing pixels |
| `reference_eew5.tif` | edge width 5.0, 8-connectivity, internal | 0 differing pixels |
| `reference_eew5_nondefault.tif` | edge width 5.0, 4-connectivity, no internal classes | 0 differing pixels |

A wider comparison against the compiled C binary was also run with edge widths
1.0–10.0, 4- and 8-connectivity, `internal` 0/1, `transition` 0/1, and
synthetic images (random rasters, corner/edge cases, holes, thin lines,
checkerboards, all-background/all-foreground/all-nodata).  All 126 cases were
pixel-identical.

### C behaviour that matters for parity

The earlier trial differed from C in a handful of border pixels.  The port now
reproduces these implementation details:

* `fm_preproc2` fills the four frame corner quadrants with the corresponding
  image corner value (the trial left them at zero);
* `binOIthin_FIFO` zeroes the outer two-pixel border of its image **and of its
  anchor** in place before thinning;
* `ced` uses a one-pixel frame and keeps its distance accumulator in float32;
* thresholds (`size*edu`) and the distance transforms are evaluated in float32,
  as in miallib, so threshold ties (for example `eew=1.02` or `4.02`) match;
* `label()` clips a one-pixel border before labelling (and miallib starts
  component ids at 2);
* `wsfah` zeroes a one-pixel border and seeds/floods neighbours in N, W, S, E
  order;
* connector segments without a flooded core id are not treated as bridges
  (the C range sentinel must not overflow into a positive range).

## Install

The launcher performs the setup automatically on first use:

```sh
cd python
./run_mspa.sh -i input.tif -o output.tif
```

If no usable `.venv` exists, it creates one with Python 3.10-3.14 and installs
`requirements.txt`.  If an existing environment is missing packages, it
installs them; if it is unusable, the script tells you to remove it and rerun.
On Ubuntu you may need `sudo apt install python3-venv` (and the matching
versioned package, for example `python3.14-venv python3-pip`).  A `.venv`
created before those packages were installed can lack `pip`; the launcher
bootstraps it with `ensurepip`, and removing `.venv` also recreates it cleanly.

Python 3.10-3.13 use Numba 0.61; Python 3.14 uses Numba 0.68, which has
Python 3.14 wheels for Linux, Windows and Apple-silicon macOS.  There is no
Numba 0.68 wheel for Intel macOS, so Python 3.14 on an Intel Mac falls back to
the slow pure-Python engine; use Python 3.12/3.13 there.  Use the standard
GIL-enabled build: the free-threaded `3.14t` build has no Numba wheel on
Linux/Windows.  On native Windows ARM64 use Python 3.12 or newer.

The batch launcher looks for `py -3.x`, `python3.x`, and the standard install
paths.  Native Windows-on-ARM installs live in a directory such as
`%LOCALAPPDATA%\Programs\Python\Python313-arm64`; that path is checked before
the x64/emulated ones.  If Python still is not found, run `py -0p` to list
registered interpreters, enable "Add Python to PATH" in the installer, or pass
the full path explicitly:

```bat
set MSPA_PYTHON=%LOCALAPPDATA%\Programs\Python\Python313-arm64\python.exe
run_mspa.bat -i input.tif -o output.tif
```

If typing `python` opens the Microsoft Store, disable the App Execution Alias
under Settings > Apps > Advanced app settings.  TBB is optional: the launcher
selects Numba's bundled `workqueue` layer by default.

Manual setup remains possible:

```sh
cd python
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
```

`numpy` and `scipy` are required.  `numba` is optional but strongly
recommended: the anchored thinning and the watershed flood are JIT-compiled.
Without numba they run as plain Python and are very slow on real-size rasters.

## Usage

The input is a 2-D `uint8` array with the same convention as the C program:

* `0` – nodata (missing)
* `1` – background
* `2` – foreground

```python
import numpy as np
from PIL import Image
from mspa import segment_binary_patterns

imin = np.asarray(Image.open("input.tif"))
out = segment_binary_patterns(
    imin,
    edge_width=1.0,   # -eew
    graphfg=8,        # -graphfg 4 or 8
    transition=1,     # -transition (palette only, see below)
    internal=1,       # -internal
)
Image.fromarray(out, mode="P").save("output.tif")
```

The result is a `uint8` class-coded image with the same values as the C
output.  `background` is `0` and `nodata` is `129`.

## Preflight for large maps

`preflight.py` reads only the image header and the local hardware, then reports
whether the in-memory engine is expected to fit and how long it should take.
It exits with status 0 only when the RAM (and optionally the output disk) is
sufficient.  The CPU report distinguishes the system count from the CPUs
actually available to the session (the value reported by `nproc`):

```text
CPUs:  178 logical (available: 3)
```

`nproc` is Linux-only: the “available” figure honours it on Linux and equals
the OS logical CPU count on macOS/Windows.  Use `-j N` to override the default
on any platform.

```sh
python preflight.py map.tif --edge-width 1.0 --output-dir /data/output
# status 0 -> safe to load and run the engine
# status 1 -> do not proceed (not enough RAM/disk)
# status 2 -> unreadable input or unknown hardware
```

Example for the CLC 10 m map (650,000 x 460,000 px) on 8 TB / 170 CPUs:

```text
Uncompressed size:  278.5 GiB (299.0 GB)
Estimated peak RAM: 9.27 TiB (10.20 TB)
Usable RAM:         6.55 TiB (7.20 TB)
RAM verdict:        NO
Shortfall:          2.73 TiB (3.00 TB)
Estimated time:     ~1.3 days
VERDICT:            DO NOT PROCEED
```

The in-memory engine therefore cannot process that map on an 8 TB machine,
even though the C binary (roughly 20 bytes/pixel) fits.  Running the preflight
first avoids loading hundreds of GB and failing days later.

With a sufficiently large scratch disk, add `--memmap-dir`:

```sh
python preflight.py map.tif --cpus 170 --available-ram-gb 8000 \
    --memmap-dir /scratch/mspa-temp --output-dir /scratch/output
```

For the CLC map this estimates about **5.68 TiB resident RAM** and
**3.71 TiB temp + output disk**; it then reports `PROCEED (MEMMAP)` if the
scratch disk has that much free space.  The mapped run is bit-identical to the
C output; only storage changes.  The C binary remains the cheaper option for
this specific map.

Large trusted maps are allowed regardless of Pillow's default
decompression-bomb threshold (the scripts set `Image.MAX_IMAGE_PIXELS = None`).
The preflight then uses the real dimensions to decide whether RAM or memmap
mode is required.

The estimate can be checked without an input file:

```sh
python preflight.py --width 650000 --height 460000 --cpus 170 \
    --available-ram-gb 8000
```

Hardware detection supports Linux (`/proc/meminfo`), macOS (`hw.memsize` and
`vm_stat`, or `psutil`), and Windows (`GlobalMemoryStatusEx`).  `--cpus`,
`--total-ram-gb`, `--available-ram-gb`, `--available-cpus`,
`--memmap-dir`, `--memmap-free-gb` and `--json` are available for scripting

and for planning runs on another machine.

The engine's default thread count follows the available (`nproc`) count.  An
explicit `-j N` overrides it; for example, `-j 100` uses 100 threads even if
`nproc` currently reports 3.

The RAM model is calibrated against the engine at 1000-8000 px, with a 25%
baseline allowance, 1.10x safety factor and a 10% RAM reserve; maps above
2³¹ pixels add an allowance for 64-bit queues.  It is
deliberately conservative: a "NO" means the current in-memory design should
not be attempted.

The time estimate is a **lower bound**, not a promise.  Only the Euclidean
distance transforms (`getcore` / `getexternalboundary`) are parallel; the
thinning, geodesic propagation, watershed and hole-filling stages are serial.
One core can therefore dominate the wall clock on large or complex maps, and
a run can take several times longer than the reported figure.  The CLI prints
per-stage timings by default (suppress with `--quiet`), so progress is visible
on long runs.

## Run from the command line

From a normal terminal (no venv activation needed), use the launcher:

```sh
cd python
./run_mspa.sh -i input.tif -o output.tif
```

On Windows:

```bat
cd python
run_mspa.bat -i input.tif -o output.tif
```

The launcher finds `.venv` automatically and runs `run_mspa.py` with it.
Alternatively, call the venv's Python explicitly:

```sh
.venv/bin/python run_mspa.py -i input.tif -o output.tif
```

or activate the environment first:

```sh
. .venv/bin/activate
python run_mspa.py -i input.tif -o output.tif
```

A plain system `python` does not have NumPy/SciPy/Numba and will fail with
`ModuleNotFoundError: No module named 'numpy'`; that is why the launcher or
the venv Python should be used.

The options mirror the C program: `-eew`, `-graphfg`, `-internal`,
`-transition`, `-odir`, and `-j` for the number of threads. The wrapper runs
the same preflight check first and refuses to start if the map is not
expected to fit. For a map that needs disk-backed processing:

```sh
./run_mspa.sh -i input.tif -o output.tif --memmap-dir /scratch/mspa-temp
```

`MSPA_PYTHON=/path/to/python` can override the interpreter used by the
launcher.

Input must follow the MSPA convention: `0` = nodata, `1` = background,
`2` = foreground. The output is a palette TIFF with the same class codes as
the C binary and the official GTB `transition1.txt` / `transition0.txt`
colour tables selected by `-transition`.

If the input is a GeoTIFF, its georeferencing tags are copied to the output.
The output also carries the GTB-style metadata:

* `ImageDescription`: `GTB_MSPA, https://forest.jrc.ec.europa.eu/activities/lpa/gtb/ <g>_<eew>_<internal>_<transition>`
* `Software`: `GTB, https://forest.jrc.ec.europa.eu/en/activities/lpa/gtb/`

For example, the default run carries the suffix `8_1_1_1`.

### Numba threads / threading layer

The engine caps `-j` at Numba's configured maximum automatically; a request
larger than the limit is reduced rather than aborting. On a very large host
this is normal (for example 192 CPUs but a Numba maximum of 178).

`-j` controls the parallel distance transforms only.  The other stages
(anchored thinning, constrained geodesic distance, watershed and hole filling)
are inherently serial and run on one core.  This matches the modest OpenMP
speed-up of the C binary (about 1.25-1.55x at 16 threads).  The CLI prints the
effective thread count and stage timings to stderr, for example:

```text
MSPA: 32,528,000,000 pixels; 150 threads (Numba max 178, layer workqueue)
MSPA: [1/8] framing and preprocessing
MSPA: [1/8] framing and preprocessing done in 35.2s
MSPA: [2/8] core distance transform
MSPA: [2/8] core distance transform done in 41.7s
MSPA: [3/8] patch / edges / holes
MSPA: setedges iteration 1
MSPA: setedges iteration 2
...
```

On maps wider or taller than 65535 px, miallib uses 64-bit squared distances;
the Python engine now uses a fused parallel 64-bit EDT for that case instead
of falling back to scipy's serial distance transform.

The launchers set `NUMBA_THREADING_LAYER=workqueue` by default.  This uses
Numba's bundled threading layer, avoids the misleading "TBB threading layer is
disabled" warning when Numba cannot discover an external TBB library, and is
still fully parallel.  To try another layer explicitly:

```sh
MSPA_NUMBA_LAYER=tbb ./run_mspa.sh -i input.tif -o output.tif
MSPA_NUMBA_LAYER=omp ./run_mspa.sh -i input.tif -o output.tif
```

Installing `tbb` into the venv is optional; the default workqueue path does
not need it.

Python 3.10-3.14 are supported when Numba provides wheels for the platform
(see the install notes above).  If Numba is unavailable the engine still
produces identical results, but runs in a much slower pure-Python mode.

## Class codes

The C code stores MSPA classes as bit planes, so several codes are sums:

| Code | Class |
|---|---|
| 0 | background |
| 1 | branch |
| 3 | edge |
| 5 | perforation |
| 9 | islet |
| 17 | core |
| 33 | bridge |
| 65 | loop |
| 35, 37, 67, 69 | transition classes (bridge/loop combined with edge/perforation) |
| 100 | internal core opening |
| 101, 103, 105, 109, 117, 133, 165 | internal twins (class + 100) |
| 220 | internal background (inside the filled foreground envelope) |
| 129 | nodata |

`transition` only changes the colour lookup table of the C output; it does not
change the class codes.  The engine therefore returns the same codes for
`transition=0` and `transition=1`.  The `-disk` option of the C program is an
I/O optimisation and is not implemented.

## Parity test

`test_parity.py` runs both reference cases and fails on the first differing
pixel:

```sh
cd python
python test_parity.py
```

Expected output:

```text
OK:   default (...): 1000000 pixels identical
OK:   edge width 1.0, 4-connectivity, ...: 1000000 pixels identical

All parity cases passed.
```

## Provenance

The engine is derived from the miallib MSPA implementation by Pierre Soille
and Peter Vogt (European Commission, Joint Research Centre), distributed
under GPL-3.0-or-later, and from the pyGuidos pure-Python work.  The algorithm
map in `mspa/engine.py` names the miallib functions each part replaces.

## Memory-mapped mode

For rasters that do not fit in RAM, pass a temporary directory:

```python
out = segment_binary_patterns(
    imin,
    edge_width=1.0,
    graphfg=8,
    internal=1,
    memmap_dir="/scratch/mspa-temp",
    memmap_min_bytes=64 * 1024 * 1024,
)
```

This is a **storage-only** change: the algorithm, traversal order, border
handling and tie-breaking are unchanged, so the output remains pixel-identical
to the C code. The current implementation maps the bridge-test arrays:
core/connector labels, their clipped copies, the flood domain and the corridor
result (about 12 bytes/pixel). Other arrays remain in RAM. Arrays smaller than
`memmap_min_bytes` stay in RAM; use `memmap_min_bytes=0` in tests to force the
mapped path.

Memory mapping is **not recommended for ordinary rasters**: with enough free
RAM, mapped pages stay cached and add page-fault/I/O overhead. It is selected
only when the raster is too large for RAM. A run needs roughly
`12 × pixels` bytes of temporary disk in addition to the output raster.

For normal maps, the default in-RAM path remains faster.

## Performance

The numbers below were measured on the same Linux x86_64 host (16 vCPU),
using a warm Numba cache.  C was run with `OMP_NUM_THREADS=16`; Python with
16 threads.  Inputs were 8-bit MSPA rasters with blob-like foreground
patterns; times are wall-clock seconds and RSS is peak resident memory.  The
first Python process compiles the Numba kernels (about 10-20 s here); later
processes reuse the Numba cache.

### Edge width 1.0 (default)

| Image | C time | C RSS | Python time | Python RSS |
|---|---:|---:|---:|---:|
| 1000 x 1000 | 1.4 s | 26 MB | 0.6 s | 201 MB |
| 2000 x 2000 | 3.3 s | 102 MB | 1.6 s | 302 MB |
| 4000 x 4000 | 11.9 s | 334 MB | 8.9 s | 663 MB |
| 8000 x 8000 | 52.4 s | 1.26 GB | 42.3 s | 1.75 GB |

### Edge width 5.0

| Image | C time | C RSS | Python time | Python RSS |
|---|---:|---:|---:|---:|
| 1000 x 1000 | 3.3 s | 26 MB | 0.9 s | 248 MB |
| 2000 x 2000 | 5.1 s | 102 MB | 2.7 s | 308 MB |
| 4000 x 4000 | 15.3 s | 335 MB | 9.6 s | 721 MB |

The Python version is now **faster than C in these benchmarks at every size**
(0.6-0.8x the C time), including 4000² and 8000².  Peak RAM is about
**1.4-2.2x C** for larger rasters (the interpreter baseline dominates at
1000 x 1000).

Runtime scales linearly with pixel count; memory is the limiting factor for
very large rasters.  Extrapolating from 8000², a 10k² image needs roughly
1.1 minute / 2.7 GB in Python versus 1.4 minutes / 2.0 GB in C; at 20k² the
figures are roughly 4.5 minutes / 11 GB versus 5.5 minutes / 8 GB.

The remaining gap at large sizes is memory bandwidth and a few full-size
temporaries, not algorithmic work.  C's OpenMP gains are themselves modest
because the thinning, geodesic propagation and watershed stages are serial
(16 threads are only about 1.25-1.55x faster than one thread).

### Implemented optimisations

* exact squared EDT in Numba with thread-chunked `prange` (cacheable, uint32
  output);
* Numba flood fill for hole filling, replacing SciPy's `binary_fill_holes`
  dilation/erosion passes;
* Numba `ced` with C-like `uint8`/`int16`/`float32` buffers;
* `uint8` output bit planes and `int32` thinning/watershed labels and queues;
* grow-only thinning queues (instead of three full-size `npix` buffers);
* a growable ring queue for the watershed, an in-place MSB-based watershed
  label image (one int32 image instead of two), and early release of the
  stage masks before the bridge test;
* adaptive index queues: `int32` below 2³¹ pixels, `int64` only for larger
  maps, so normal-size rasters keep the smaller memory footprint;
* a fused parallel 64-bit EDT for maps wider/taller than 65535 px, matching
  miallib's `t_UINT64` path instead of falling back to SciPy's serial EDT;
* per-stage progress/timing output in the CLI (`--quiet` to suppress), so
  long serial stages are visible;
* a storage-only memory-mapped mode for rasters beyond RAM (see above), with
  no change to the algorithm or results.

Without Numba the engine still works but the thinning, geodesic and EDT
stages fall back to plain Python/SciPy and are much slower for large rasters.
