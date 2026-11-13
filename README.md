# MSPA – Morphological Spatial Pattern Analysis (standalone build)

[![build](https://github.com/vogtpet/MSPAstandalone/actions/workflows/build.yml/badge.svg)](https://github.com/vogtpet/MSPAstandalone/actions/workflows/build.yml)

This repository contains the complete source code and build setup needed to
compile the standalone MSPA command line tool (version 2.4) for five
operating system / CPU architecture combinations. The resulting binaries are
self-contained: they do not require a Python, GDAL, PROJ or miallib
installation on the target machine.

MSPA is a sequence of mathematical morphological operators that classifies
the foreground of a binary image into core, islet, perforation, edge, loop,
bridge, branch and background, together with transition classes.

References:

* P. Soille and P. Vogt (2009): *Morphological segmentation of binary
  patterns*. Pattern Recognition Letters 30(4):456–459.
  <https://doi.org/10.1016/j.patrec.2008.10.015>
* P. Soille and P. Vogt (2022): *Morphological Spatial Pattern Analysis:
  open source release*. ISPRS Archives XLVIII-4/W1-2022, 427–433.
  <https://doi.org/10.5194/isprs-archives-XLVIII-4-W1-2022-427-2022>

MSPA is also part of [GuidosToolbox](https://forest.jrc.ec.europa.eu/en/activities/lpa/gtb/)
and the GuidosToolbox Workbench.

## Supported platforms

| Target        | Operating system            | CPU        | Toolchain                     |
|---------------|-----------------------------|------------|-------------------------------|
| `Linux`       | Linux (glibc, e.g. Debian 13) | x86_64   | GCC (distribution packages)   |
| `Linux_ARM`   | Linux (glibc, e.g. Debian 13) | aarch64  | GCC (distribution packages)   |
| `macOS_ARM`   | macOS (Apple Silicon)       | Apple Silicon | Homebrew GCC (auto-detected) |
| `Windows`     | Windows 10/11               | x86_64     | MSYS2 UCRT64 (GCC)            |
| `Windows_ARM` | Windows 11                  | ARM64      | MSYS2 CLANGARM64 (Clang)      |

The binaries are built with generic CPU tuning (no `-march=native` or other
CPU-specific instruction sets), so a binary built for `x86_64` runs on any
Intel or AMD x86_64 CPU, and a binary built for `aarch64` runs on any ARMv8-A
compatible CPU. The minimum macOS version of the macOS binary is the
deployment target of your GCC toolchain (macOS 11 or later for Apple Silicon);
lower it with `-mmacosx-version-min` in `Makefile_macOS_ARM` if needed.

## Download

Prebuilt binaries are attached to the
[releases](https://github.com/vogtpet/MSPAstandalone/releases):

| Archive | Platform |
|---|---|
| `mspa-2.4-linux-x86_64.tar.gz` | Linux x86_64 |
| `mspa-2.4-linux-aarch64.tar.gz` | Linux aarch64 / ARM64 |
| `mspa-2.4-macos-arm64.tar.gz` | macOS Apple Silicon |
| `mspa-2.4-windows-x86_64.zip` | Windows x86_64 |
| `mspa-2.4-windows-arm64.zip` | Windows ARM64 |

Every release also includes a `SHA256SUMS` file:

```sh
sha256sum -c SHA256SUMS       # Linux
shasum -a 256 -c SHA256SUMS   # macOS
```

On Windows, check a file with `certutil -hashfile <file> SHA256`.

The Linux binaries are built on Debian 13 and require **glibc 2.41 or newer**.
If you need to run MSPA on a distribution with an older glibc, build it on an
older distribution (for example Debian 11, glibc 2.31); see
[Building](#building).

The macOS archive contains the executable **and** the two dynamic libraries
(`libdeflate.0.dylib`, `libLerc.4.dylib`). Keep all three files in the same
directory; do not copy only the executable.

The macOS binary is ad-hoc signed and not notarised, so Gatekeeper warns about
downloaded files. After extracting the archive, clear the quarantine from the
whole folder and run it:

```sh
cd mspa-2.4-macos-arm64
xattr -cr .
./mspa_macOS_ARM -help
```

Clearing only the executable is not enough — the two dylibs are quarantined
too. Only a Developer ID signature plus Apple notarisation avoids the warning
entirely.

The Windows binaries are not code-signed, so SmartScreen may show a warning;
choose “More info” → “Run anyway” after verifying the checksum.

## Tested on

* **Linux x86_64** – built on Debian 13; the Debian 11 build (glibc 2.31) was
  tested on Debian, Ubuntu, Fedora and openSUSE
* **Linux aarch64** – Debian 13 (native and CI)
* **macOS ARM64** – Apple Silicon with Homebrew GCC
* **Windows x86_64** – Windows 10/11 with MSYS2 UCRT64
* **Windows ARM64** – Windows 11 with MSYS2 CLANGARM64

All five targets produce pixel-identical output for `testing/input.tif`; the
`deploy.sh` smoke test and the CI matrix verify this automatically.

## Repository layout

```
Makefile_Linux          driver makefiles (one per target)
Makefile_Linux_ARM
Makefile_macOS_ARM
Makefile_Windows
Makefile_Windows_ARM
deploy.sh               builds the library, the binary and the distribution
corridor_lightmemt.c    MSPA driver (legacy name; referred to as fsp.c)
frag.c / frag.h         command line handling
mia_parse.c / .h        generic argument parser
mial-master/            miallib source code and public headers
                        (the driver includes them directly; see
                        "Upstream modifications")
testing/input.tif       test image used by the automatic smoke test
testing/reference*.tif  expected pixel output (checked by deploy.sh)
testing/tiffcmp.c       libtiff based pixel comparison tool
.github/workflows/      CI build matrix (five targets)
CITATION.cff            machine-readable citation metadata
RELEASE.md              release checklist
packaging/              release packaging script
ReadmeCompile.docx      detailed build instructions (Word document)
LICENSE                 GNU General Public License v3
```

## Building

All targets are built with a single script. The script has to be run from a
bash shell (Linux, macOS, MSYS2) and needs `make`, a C compiler and the
platform libraries listed below. It refuses to build a target that does not
match the host operating system and CPU architecture, because the makefiles
do not cross-compile. The compiler can be overridden with `MSPA_CC=<compiler>`;
on macOS the installed Homebrew GCC is detected automatically.

### Linux x86_64 and Linux aarch64

Use Debian 13 (or a distribution with the same or a newer glibc) because the
static libraries needed for linking are available as distribution packages:

```sh
sudo apt update
sudo apt install build-essential gcc g++ make libgomp1 \
    libtiff-dev libdeflate-dev libpng-dev libwebp-dev libjpeg-dev \
    libjbig-dev liblerc-dev libsharpyuv-dev libzstd-dev \
    liblzma-dev zlib1g-dev
```

Then run one of:

```sh
./deploy.sh Linux
./deploy.sh Linux_ARM
```

The Linux binaries are dynamically linked against the system C library. A
binary built on Debian 13 (glibc 2.41) runs on distributions with glibc 2.41
or newer. Build on an older distribution if you need to support older
systems.

### MS-Windows x86_64 (MSYS2 UCRT64)

Install [MSYS2](https://www.msys2.org/), open the **UCRT64** terminal and run:

```sh
pacman -Syu
pacman -S --needed base-devel \
    mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-gcc \
    mingw-w64-ucrt-x86_64-gcc-libs mingw-w64-ucrt-x86_64-make \
    mingw-w64-ucrt-x86_64-libpng mingw-w64-ucrt-x86_64-libwebp \
    mingw-w64-ucrt-x86_64-libjpeg-turbo mingw-w64-ucrt-x86_64-jbigkit \
    mingw-w64-ucrt-x86_64-lerc \
    mingw-w64-ucrt-x86_64-zstd mingw-w64-ucrt-x86_64-xz \
    mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-libtiff \
    mingw-w64-ucrt-x86_64-libdeflate
```

Copy the repository into your MSYS2 home directory
(`C:\msys64\home\<user>\`), change into it and run:

```sh
./deploy.sh Windows
```

### MS-Windows ARM64 (MSYS2 CLANGARM64)

Install [MSYS2](https://www.msys2.org/) with the CLANGARM64 environment, open
the **CLANGARM64** terminal and run:

```sh
pacman -Syu
pacman -S --needed \
    mingw-w64-clang-aarch64-clang mingw-w64-clang-aarch64-llvm-openmp \
    mingw-w64-clang-aarch64-toolchain mingw-w64-clang-aarch64-gcc-compat \
    mingw-w64-clang-aarch64-libtiff mingw-w64-clang-aarch64-libpng \
    mingw-w64-clang-aarch64-libwebp mingw-w64-clang-aarch64-libjpeg-turbo \
    mingw-w64-clang-aarch64-jbigkit mingw-w64-clang-aarch64-lerc \
    mingw-w64-clang-aarch64-zstd mingw-w64-clang-aarch64-xz \
    mingw-w64-clang-aarch64-zlib mingw-w64-clang-aarch64-libdeflate \
    make git
```

Then run:

```sh
./deploy.sh Windows_ARM
```

### macOS (Apple Silicon)

Install [Homebrew](https://brew.sh/) and the required packages:

```sh
brew install gcc liblerc libdeflate libtiff jpeg-turbo \
    libpng webp jbigkit zstd xz
```

Then run:

```sh
./deploy.sh macOS_ARM
```

The installed Homebrew GCC is detected automatically (the newest version if
several are installed), so the build keeps working after a GCC upgrade. To
select a specific compiler, set `MSPA_CC`:

```sh
MSPA_CC=/opt/homebrew/bin/gcc-15 ./deploy.sh macOS_ARM
```

`deploy.sh` bundles the two remaining dynamic libraries (`libdeflate` and
`libLerc`) and rewrites their paths to `@executable_path`, so the whole
`mspa_dist_macOS_ARM/` directory can be copied to another Mac without
Homebrew.

## What `deploy.sh` does

1. checks that the requested target matches the host OS and CPU architecture;
2. builds the static library `libmiallib_mspa.a` from `mial-master/core/c`;
3. compiles the MSPA driver with the platform makefile;
4. creates `mspa_dist_<target>/` and copies the executable;
5. on macOS, bundles and re-signs the dynamic libraries;
6. runs the test image `testing/input.tif` twice (default parameters and
   `-graphfg 4 -internal 0`) and compares both outputs pixel by pixel with
   the reference images;
7. removes the intermediate object files and the static library.

If any step fails, the script stops with a non-zero exit status.

## Testing

`deploy.sh` runs two test cases and compares both results pixel by pixel with
the reference images in `testing/`:

```sh
./deploy.sh macOS_ARM
# ... builds and runs:
#   mspa -i input.tif -o output.tif -odir ./
#   mspa -i input.tif -o output_nondefault.tif -odir ./ -graphfg 4 -internal 0
# ... then verifies output*.tif against testing/reference*.tif
```

The comparison uses `testing/tiffcmp.c`, a small tool built against libtiff
(no Python, GDAL or ImageMagick needed). It fails the build if a single pixel
differs. Set `MSPA_SKIP_VERIFY=1` to skip this check, for example while
experimenting with a modified algorithm.

The TIFF output contains a `Software` tag and a timestamp, so two runs are not
byte-identical; only the pixel values are compared. `testing/compare_outputs.py`
is an alternative comparison script (requires Pillow), and
`testing/README.md` lists the expected class counts for the test image.

## Continuous integration

[`.github/workflows/build.yml`](.github/workflows/build.yml) builds and tests
all five targets on GitHub-hosted runners and uploads each distribution
directory as a workflow artifact:

| Job | Runner |
|-----|--------|
| `Linux` | `ubuntu-24.04` |
| `Linux_ARM` | `ubuntu-24.04-arm` |
| `Windows` | `windows-2025` (MSYS2 UCRT64) |
| `Windows_ARM` | `windows-11-arm` (MSYS2 CLANGARM64) |
| `macOS_ARM` | `macos-15` (Apple Silicon) |

All ARM jobs run natively, so the CI results are not affected by emulation.

## Command line

```
mspa -i <input> [-o <output>] [-odir <dir>] [-eew <width>]
     [-graphfg <4|8>] [-internal <0|1>] [-transition <0|1>] [-disk]
     [-v] [-help|-usage]
```

| Option        | Meaning                                                        | Default     |
|---------------|----------------------------------------------------------------|-------------|
| `-i <file>`   | input image (binary or grey level, TIFF or other miallib format) | required  |
| `-o <file>`   | output image                                                   | `out.tif`   |
| `-odir <dir>` | output directory                                               | `./`        |
| `-eew <n>`    | effective edge width                                           | `1.0`       |
| `-graphfg <n>`| foreground connectivity (4 or 8)                               | `8`         |
| `-internal <n>` | include internal holes/disconnected foreground (0/1)         | `1`         |
| `-transition <n>` | include transition classes (0/1)                           | `1`         |
| `-disk`       | minimum RAM mode: spool intermediate images to disk            | off         |
| `-v`          | verbose parameter output                                       | off         |

Output pixel values follow the MSPA colour code (1 = core, 3 = perforation,
5 = edge, 9 = loop, 17 = bridge, 33 = branch, 65 = islet, 129 = background,
220 = internal hole; transition classes add 34/36/38/... for the 8-connected
foreground).

## Citation

If you use MSPA in a publication, please cite:

* P. Soille and P. Vogt (2009): *Morphological segmentation of binary
  patterns*. Pattern Recognition Letters 30(4):456–459.
  <https://doi.org/10.1016/j.patrec.2008.10.015>
* P. Soille and P. Vogt (2022): *Morphological Spatial Pattern Analysis:
  open source release*. The International Archives of the Photogrammetry,
  Remote Sensing and Spatial Information Sciences, XLVIII-4/W1-2022, 427–433.
  <https://doi.org/10.5194/isprs-archives-XLVIII-4-W1-2022-427-2022>

```bibtex
@article{soille2009morphological,
  author  = {Soille, Pierre and Vogt, Peter},
  title   = {Morphological segmentation of binary patterns},
  journal = {Pattern Recognition Letters},
  volume  = {30},
  number  = {4},
  pages   = {456--459},
  year    = {2009},
  doi     = {10.1016/j.patrec.2008.10.015}
}

@article{soille2022morphological,
  author  = {Soille, Pierre and Vogt, Peter},
  title   = {Morphological Spatial Pattern Analysis: open source release},
  journal = {The International Archives of the Photogrammetry, Remote Sensing and Spatial Information Sciences},
  volume  = {XLVIII-4/W1-2022},
  pages   = {427--433},
  year    = {2022},
  doi     = {10.5194/isprs-archives-XLVIII-4-W1-2022-427-2022}
}
```

A machine-readable version is available in [`CITATION.cff`](CITATION.cff).

## License

MSPA and miallib are free software: you can redistribute them and/or modify
them under the terms of the **GNU General Public License, version 3 or later**
(GPL-3.0-or-later). See [LICENSE](LICENSE) for the full text.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.

## Third-party components

The binaries link the following libraries. Their licences are compatible with
the GPL; the upstream notices are retained in `mial-master/` and in the
respective library packages.

* miallib (JRC, GPL-3.0-or-later) – image processing library
* libtiff (libtiff licence), libjpeg-turbo (BSD/IJG), libpng (libpng licence),
  libwebp (BSD), jbigkit (GPL-2.0-or-later), zlib (zlib licence), zstd (BSD/GPL-2.0),
  xz/liblzma (public domain), libdeflate (MIT), Lerc (Apache-2.0)

## Upstream modifications

`mial-master/` is a snapshot of the JRC miallib source. The upstream code was
written for Linux x86_64. The following changes make the same source tree
compile and run on Windows and macOS, on x86_64 and aarch64:

* the build system detects the target architecture and the compiler
  (GCC/Clang) and uses the matching OpenMP flags;
* the FIFO used by the connected-component labelling stores pointers in
  `intptr_t` instead of `long int`, which is 32 bit on 64 bit Windows;
* the makefile builds only the subset of miallib needed by MSPA and no longer
  depends on GDAL, PROJ or FFTW;
* the `FSP` build flag is passed to the library so that the MSPA-specific
  TIFF software tag and input handling remain unchanged.

The exact list of modified files is in
[`mial-master/MODIFICATIONS.md`](mial-master/MODIFICATIONS.md).

## Documentation

`ReadmeCompile.docx` contains the detailed, illustrated build instructions for
all supported platforms. This README is the quick reference.
