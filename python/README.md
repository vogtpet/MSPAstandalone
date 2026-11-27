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
| 1000 x 1000 | 1.4 s | 26 MB | 0.9 s | 208 MB |
| 2000 x 2000 | 3.3 s | 102 MB | 2.4 s | 329 MB |
| 4000 x 4000 | 11.9 s | 334 MB | 13.5 s | 796 MB |
| 8000 x 8000 | 52.4 s | 1.26 GB | 69.3 s | 2.63 GB |

### Edge width 5.0

| Image | C time | C RSS | Python time | Python RSS |
|---|---:|---:|---:|---:|
| 1000 x 1000 | 3.3 s | 26 MB | 1.2 s | 252 MB |
| 2000 x 2000 | 5.1 s | 102 MB | 3.8 s | 335 MB |
| 4000 x 4000 | 15.3 s | 335 MB | 14.6 s | 831 MB |

The Python version is now **faster than C on 1000² and 2000² rasters**, and
about **1.1-1.3x slower at 4000² and 8000²** for edge width 1.  Peak RAM is
about **1.6-2.5x C** for larger rasters (the interpreter baseline dominates
at 1000 x 1000).

Runtime scales linearly with pixel count; memory is the limiting factor for
very large rasters.  Extrapolating from 8000², a 10k² image needs roughly
1.8 minutes / 4.1 GB in Python versus 1.4 minutes / 2.0 GB in C; at 20k² the
figures are roughly 7 minutes / 16 GB versus 5.5 minutes / 8 GB.

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
* `uint8` output bit planes and `int32` thinning/watershed labels and queues.

Further memory reductions would need dynamic (grow-only) queues for the
thinning/watershed stages, and for images beyond RAM a memory-mapped or tiled
variant with halos (the C program's `-disk` mode is the equivalent).

Without Numba the engine still works but the thinning, geodesic and EDT
stages fall back to plain Python/SciPy and are much slower for large rasters.
