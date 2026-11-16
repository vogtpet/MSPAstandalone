## MSPA 2.4 standalone builds

Prebuilt binaries for five platforms, plus a Linux x86_64 compatibility
build for older glibc systems. No Python, GDAL, PROJ or miallib installation
is required on the target machine.

| Platform | File |
|---|---|
| Linux x86_64 | `mspa-2.4-linux-x86_64.tar.gz` |
| Linux x86_64 (older glibc) | `mspa-2.4-linux-x86_64-glibc2.31.tar.gz` |
| Linux aarch64 | `mspa-2.4-linux-aarch64.tar.gz` |
| macOS Apple Silicon | `mspa-2.4-macos-arm64.tar.gz` |
| Windows x86_64 | `mspa-2.4-windows-x86_64.zip` |
| Windows ARM64 | `mspa-2.4-windows-arm64.zip` |

The default Linux binaries are built on Debian 13 and require glibc 2.41 or
newer. The extra `mspa-2.4-linux-x86_64-glibc2.31.tar.gz` archive is built on
Debian 11 and requires only glibc 2.31 or newer; use it on older distributions
(for example Ubuntu 20.04/22.04 or Debian 11). It produces identical MSPA
output. For other architectures, build from source on an older release; see
the README.

The macOS archive contains the executable and two dynamic libraries; keep
them together. The binary is ad-hoc signed and not notarised. After
extracting, clear the quarantine from the whole folder and run it:

    cd mspa-2.4-macos-arm64
    xattr -cr .

Clearing only the executable is not enough — the two dylibs are quarantined
too. Only a Developer ID signature plus Apple notarisation avoids the
warning entirely.

The Windows binaries are unsigned; SmartScreen may show a warning.

Verify the downloads with the attached `SHA256SUMS` file.

All five targets produce pixel-identical output for the test image
`testing/input.tif`; the build and the CI matrix verify this automatically.

### Citation

If you use MSPA in a publication, please cite:

- P. Soille and P. Vogt (2009): *Morphological segmentation of binary
  patterns*. Pattern Recognition Letters 30(4):456–459.
  https://doi.org/10.1016/j.patrec.2008.10.015
- P. Soille and P. Vogt (2022): *Morphological Spatial Pattern Analysis:
  open source release*. ISPRS Archives XLVIII-4/W1-2022, 427–433.
  https://doi.org/10.5194/isprs-archives-XLVIII-4-W1-2022-427-2022
