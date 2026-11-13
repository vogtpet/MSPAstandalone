# Release checklist

This document describes how to produce a release of the standalone MSPA
binaries. The current version is **2.4** (the same version shown by
`mspa -help` and written into the TIFF metadata).

## 1. Prepare the source

- `git status` must be clean and `main` up to date.
- The CI matrix must be green on the commit that will be tagged.
- If the version changes, update:
  - `mia_parse.h` (`MIA_ADVERTISE`)
  - `corridor_lightmemt.c` (the `desc` string)
  - `mial-master/core/c/imio.c` (the TIFF `Software` tag)
  - `README.md` (archive names and version)
  - `CITATION.cff` (`version`, `date-released`)
- Commit and push.

## 2. Build all five targets

Build from the same commit on each machine. `deploy.sh` refuses a target that
does not match the host and runs both pixel comparisons:

| Target | Build host | Command |
|---|---|---|
| `Linux` | Debian 13 x86_64 | `./deploy.sh Linux` |
| `Linux_ARM` | Debian 13 aarch64 | `./deploy.sh Linux_ARM` |
| `macOS_ARM` | Apple Silicon, Homebrew GCC | `./deploy.sh macOS_ARM` |
| `Windows` | MSYS2 UCRT64 | `./deploy.sh Windows` |
| `Windows_ARM` | MSYS2 CLANGARM64 | `./deploy.sh Windows_ARM` |

The official Linux binaries are built on Debian 13 and require glibc 2.41 or
newer. A Debian 11 build (glibc 2.31) can be produced as a separate
compatibility build if needed; keep the two clearly labelled.

## 3. Collect the distribution directories

Copy the five `mspa_dist_*` directories from the build machines into the
repository root:

```
mspa_dist_linux/
mspa_dist_linux_ARM/
mspa_dist_macOS_ARM/
mspa_dist_windows/
mspa_dist_windows_ARM/
```

They are ignored by git. Do not commit them.

## 4. Package

```sh
./packaging/package_release.sh 2.4
```

This creates `release/mspa-2.4/` with one archive per platform (including
`LICENSE`, `README.md` and `CITATION.cff`) plus `SHA256SUMS`.

## 5. Tag

```sh
git tag -a v2.4 -m "MSPA 2.4 standalone builds"
git push origin v2.4
```

## 6. Create the release

With the GitHub CLI:

```sh
gh release create v2.4 \
  release/mspa-2.4/*.tar.gz \
  release/mspa-2.4/*.zip \
  release/mspa-2.4/SHA256SUMS \
  --title "MSPA 2.4 standalone builds" \
  --notes-file RELEASE_NOTES.md
```

Or create the release in the web UI and upload the same files manually.

## 7. Release notes template

```markdown
## MSPA 2.4 standalone builds

Prebuilt binaries for five platforms. No Python, GDAL, PROJ or miallib
installation is required on the target machine.

| Platform | File |
|---|---|
| Linux x86_64 | `mspa-2.4-linux-x86_64.tar.gz` |
| Linux aarch64 | `mspa-2.4-linux-aarch64.tar.gz` |
| macOS Apple Silicon | `mspa-2.4-macos-arm64.tar.gz` |
| Windows x86_64 | `mspa-2.4-windows-x86_64.zip` |
| Windows ARM64 | `mspa-2.4-windows-arm64.zip` |

The Linux binaries are built on Debian 13 and require glibc 2.41 or newer.
For older distributions, build from source on an older release (for example
Debian 11, glibc 2.31); see the README.

The macOS archive contains the executable and two dynamic libraries; keep
them together. The binary is ad-hoc signed, not notarised, so macOS shows
the Gatekeeper warning “Apple could not verify …” on first launch. Remove
the quarantine attribute from the extracted folder:

    xattr -cr mspa-2.4-macos-arm64

Clearing only the executable is not enough: macOS quarantines the two dylibs
as well, and Gatekeeper then reports `libdeflate.0.dylib` on the next launch.

Or right-click the executable and choose Open, or allow it under System
Settings → Privacy & Security → Open Anyway. Re-signing the binary locally
does not remove the warning; only an Apple Developer ID signature and
notarisation do.

The Windows binaries are unsigned; SmartScreen may show a warning.

Verify the downloads with the attached `SHA256SUMS` file.

All five targets produce pixel-identical output for the test image
`testing/input.tif`; the build and the CI matrix verify this automatically.
```

## 8. Verify

- Download each asset from the release page and check `SHA256SUMS`.
- Run `mspa -help` and the test image on at least one machine per platform.
- Confirm that the macOS archive contains both dylibs.

## 9. Post-release

- If the release date changed, update `date-released` in `CITATION.cff`.
- Announce the release (GuidosToolbox/GWB channels, if applicable).
