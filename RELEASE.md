# Release checklist

This document describes how to produce a release of the standalone MSPA
binaries. The MSPA engine version is **2.4** (the same version shown by
`mspa -help` and written into the TIFF metadata).  Release `v2.4.2` is the
first release built from the optimized C implementation; the original C
source is archived at tag `c-baseline-v2.4`.

## 1. Prepare the source

- `git status` must be clean and `main` up to date.
- The CI matrix must be green on the commit that will be tagged.
- If the version changes, update:
  - `source/mia_parse.h` (`MIA_ADVERTISE`)
  - `source/corridor_lightmemt.c` (the `desc` string)
  - `source/mial-master/core/c/imio.c` (the TIFF `Software` tag)
  - `README.md` (archive names and version)
  - `CITATION.cff` (`version`, `date-released`)
- Commit and push.

## 2. Build all five targets

Build from the same commit on each machine. `source/deploy.sh` refuses a target that
does not match the host and runs both pixel comparisons:

| Target | Build host | Command |
|---|---|---|
| `Linux` | Ubuntu 22.04 x86_64 (glibc 2.35) | `./source/deploy.sh Linux` |
| `Linux_ARM` | Ubuntu 22.04 aarch64 (glibc 2.35) | `./source/deploy.sh Linux_ARM` |
| `macOS_ARM` | Apple Silicon, Homebrew GCC | `./source/deploy.sh macOS_ARM` |
| `Windows` | MSYS2 UCRT64 | `./source/deploy.sh Windows` |
| `Windows_ARM` | MSYS2 CLANGARM64 | `./source/deploy.sh Windows_ARM` |

The default Linux x86_64 and aarch64 binaries are built on Ubuntu 22.04 and
require glibc 2.35 or newer.  This covers Ubuntu 22.04/24.04 and Debian
12/13.  Also produce a Debian 11 build (glibc 2.31) as a compatibility
archive; name it `mspa-<version>-linux-x86_64-glibc2.31.tar.gz` and add it to
`SHA256SUMS`.  Ubuntu 22.04 and Debian 11 do not provide a separate static
`libsharpyuv`; the makefiles detect it and omit it automatically.

## 3. Collect the distribution directories

Copy the five `mspa_dist_*` directories from the build machines into the
`source/` directory:

```
source/mspa_dist_linux/
source/mspa_dist_linux_ARM/
source/mspa_dist_macOS_ARM/
source/mspa_dist_windows/
source/mspa_dist_windows_ARM/
```

They are ignored by git. Do not commit them.

## 4. Package

```sh
./packaging/package_release.sh 2.4.2
```

This creates `release/mspa-2.4.2/` with one archive per platform (including
`LICENSE`, `README.md` and `CITATION.cff`) plus `SHA256SUMS`.

## 5. Tag

```sh
git tag -a v2.4.2 -m "MSPA 2.4.2 standalone builds (optimized C)"
git push origin v2.4.2
```

## 6. Create the release

With the GitHub CLI:

```sh
gh release create v2.4.2 \
  release/mspa-2.4.2/*.tar.gz \
  release/mspa-2.4.2/*.zip \
  release/mspa-2.4.2/SHA256SUMS \
  --title "MSPA 2.4.2 standalone builds (optimized C)" \
  --notes-file RELEASE_NOTES.md
```

Or create the release in the web UI and upload the same files manually.

## 7. Release notes template

```markdown
## MSPA 2.4.2 standalone builds (optimized C)

Prebuilt binaries for five platforms plus a Linux x86_64 compatibility build.
No Python, GDAL, PROJ or miallib installation is required on the target
machine.  The MSPA engine remains version 2.4.

This release contains the optimized C implementation.  The output is
pixel-identical to the archived baseline (`c-baseline-v2.4`), including a
214,000×152,000 Turkey map.  On that map the optimized build was about 20%
faster and reduced the measured peak-RAM factor from about 21× to about 18×
the uncompressed input size.

| Platform | File |
|---|---|
| Linux x86_64 | `mspa-2.4.2-linux-x86_64.tar.gz` |
| Linux x86_64 (older glibc) | `mspa-2.4.2-linux-x86_64-glibc2.31.tar.gz` |
| Linux aarch64 | `mspa-2.4.2-linux-aarch64.tar.gz` |
| macOS Apple Silicon | `mspa-2.4.2-macos-arm64.tar.gz` |
| Windows x86_64 | `mspa-2.4.2-windows-x86_64.zip` |
| Windows ARM64 | `mspa-2.4.2-windows-arm64.zip` |

The default Linux x86_64 and aarch64 binaries are built on Ubuntu 22.04 and
require glibc 2.35 or newer.  The extra `...-glibc2.31.tar.gz` archive is
built on Debian 11 and requires only glibc 2.31 or newer.  All produce
identical MSPA output.

The macOS archive contains the executable and two dynamic libraries; keep
them together. The binary is ad-hoc signed and not notarised. After
extracting, clear the quarantine from the whole folder and run it:

    cd mspa-2.4.2-macos-arm64
    xattr -cr .

Clearing only the executable is not enough — the two dylibs are quarantined
too. Only a Developer ID signature plus Apple notarisation avoids the
warning entirely.

The Windows binaries are unsigned; SmartScreen may show a warning.

Verify the downloads with the attached `SHA256SUMS` file.
```

## 8. Verify

- Download each asset from the release page and check `SHA256SUMS`.
- Run `mspa -help` and the test image on at least one machine per platform.
- Confirm that the macOS archive contains both dylibs.

## 9. Post-release

- If the release date changed, update `date-released` in `CITATION.cff`.
- Announce the release (GuidosToolbox/GWB channels, if applicable).
