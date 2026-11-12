# MSPA – Morphological Spatial Pattern Analysis (standalone build)

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
| `macOS_ARM`   | macOS 11 or later           | Apple Silicon | Homebrew GCC (`gcc-16`)    |
| `Windows`     | Windows 10/11               | x86_64     | MSYS2 UCRT64 (GCC)            |
| `Windows_ARM` | Windows 11                  | ARM64      | MSYS2 CLANGARM64 (Clang)      |

The binaries are built with generic CPU tuning (no `-march=native` or other
CPU-specific instruction sets), so a binary built for `x86_64` runs on any
Intel or AMD x86_64 CPU, and a binary built for `aarch64` runs on any ARMv8-A
compatible CPU.

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
miallib.h, mialtypes.h, op.h, config_miallib.h
                        public miallib headers (copies of mial-master/core/c)
mial-master/            miallib source code (see "Upstream modifications")
testing/input.tif       test image used by the automatic smoke test
ReadmeCompile.docx      detailed build instructions (Word document)
LICENSE                 GNU General Public License v3
```

## Building

All targets are built with a single script. The script has to be run from a
bash shell (Linux, macOS, MSYS2) and needs `make`, a C compiler and the
platform libraries listed below.

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
    mingw-w64-clang-aarch64-clang mingw-w64-clang-aarch64-openmp \
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

The makefile and `deploy.sh` use `gcc-16` (the current Homebrew GCC). If you
have a different GCC version, pass it explicitly:

```sh
MSPA_CC=gcc-15 ./deploy.sh macOS_ARM
```

`deploy.sh` bundles the two remaining dynamic libraries (`libdeflate` and
`libLerc`) and rewrites their paths to `@executable_path`, so the whole
`mspa_dist_macOS_ARM/` directory can be copied to another Mac without
Homebrew.

## What `deploy.sh` does

1. builds the static library `libmiallib_mspa.a` from `mial-master/core/c`;
2. compiles the MSPA driver with the platform makefile;
3. creates `mspa_dist_<target>/` and copies the executable;
4. on macOS, bundles and re-signs the dynamic libraries;
5. runs the test image `testing/input.tif` twice (default parameters and
   `-graphfg 4 -internal 0`) and checks that both output files were created;
6. removes the intermediate object files and the static library.

If any step fails, the script stops with a non-zero exit status.

## Testing

The smoke test in `deploy.sh` is also a convenient way to check a build:

```sh
cd testing
./mspa_macOS_ARM -i input.tif -o output.tif -odir ./
```

The TIFF output contains a `Software` tag and a timestamp, so two runs are
not necessarily byte-identical. Compare the pixel values (for example with
`gdalinfo -checksum` or the bundled `testing/compare_outputs.py` script) when
you compare results between platforms. `testing/README.md` lists the expected
class counts for the test image.

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
  libwebp (BSD), jbigkit (GPL-2.0), zlib (zlib licence), zstd (BSD/GPL-2.0),
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

## Documentation

`ReadmeCompile.docx` contains the detailed, illustrated build instructions for
all supported platforms. This README is the quick reference.
