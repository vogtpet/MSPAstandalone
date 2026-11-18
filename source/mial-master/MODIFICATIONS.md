# Local modifications to miallib

This directory is a snapshot of the JRC miallib source used by the standalone
MSPA build (upstream snapshot of 2026-10-20, see `../ReadmeCompile.docx`).
The upstream code targets Linux x86_64. The changes below let the same source
tree compile and run on Linux, MS-Windows and macOS, on x86_64 and aarch64.

## Files changed

`core/c/Makefile`

* detect aarch64/ARM64 and the MSYS2 CLANGARM64 environment,
* use the OpenMP flags of the active compiler (GCC vs Clang),
* respect `CC`/`CXX` given on the command line or in the environment,
* accept platform include paths and other flags through `EXTRA_CFLAGS`,
* build only the subset of miallib needed by MSPA, with `-DFSP`, and without
  GDAL, PROJ or FFTW,
* removed the author-specific include paths (`/mnt/...`, `${CONDA_PREFIX}`).

`core/c/fifo.c`, `core/c/fifo.h`, `core/c/label.c`

* the FIFO queue stores pointers in `intptr_t` instead of `long int`.
  `long` is 32 bit on 64 bit Windows, so the original code truncated pointers
  when the queue was enlarged.

`core/c/imio.c`

* the TIFF Software tag for the FSP/MSPA build now reads `mspa v2.4`
  (upstream: `mspa v2.0`).

## Reference

The remaining source files are unchanged from the upstream snapshot.
