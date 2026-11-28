## MSPA 2.4.2 standalone builds (optimized C)

Prebuilt binaries for five platforms plus a Linux x86_64 compatibility build.
No Python, GDAL, PROJ or miallib installation is required on the target
machine. The MSPA engine remains **version 2.4**.

This release contains the optimized C implementation. The algorithm and every
output class are unchanged: the result is pixel-identical to the archived
baseline (`c-baseline-v2.4`), including a 214,000×152,000 Turkey map. On that
map the optimized build was about 20% faster and reduced the measured peak-RAM
factor from about 21× to about **18×** the uncompressed input size.

| Platform | File |
|---|---|
| Linux x86_64 | `mspa-2.4.2-linux-x86_64.tar.gz` |
| Linux x86_64 (older glibc) | `mspa-2.4.2-linux-x86_64-glibc2.31.tar.gz` |
| Linux aarch64 | `mspa-2.4.2-linux-aarch64.tar.gz` |
| macOS Apple Silicon | `mspa-2.4.2-macos-arm64.tar.gz` |
| Windows x86_64 | `mspa-2.4.2-windows-x86_64.zip` |
| Windows ARM64 | `mspa-2.4.2-windows-arm64.zip` |

The default Linux x86_64 and aarch64 binaries are built on Ubuntu 22.04 and
require **glibc 2.35 or newer**. The extra `...-glibc2.31.tar.gz` archive is
built on Debian 11 and requires only **glibc 2.31 or newer**. All variants
produce identical MSPA output.

The macOS archive contains a single self-contained executable. The binary
is ad-hoc signed and not notarised. After extracting, clear the quarantine
and run it:

```sh
cd mspa-2.4.2-macos-arm64
xattr -cr .
```

Only a Developer ID signature plus Apple notarisation avoids the warning
entirely.

The Windows binaries are unsigned; SmartScreen may show a warning.

Verify the downloads with the attached `SHA256SUMS` file.

### Also available

- Original C implementation: tag [`c-baseline-v2.4`](https://github.com/vogtpet/MSPAstandalone/tree/c-baseline-v2.4) (see `source/ARCHIVE.md`).
- Previous release: [`v2.4.1`](https://github.com/vogtpet/MSPAstandalone/releases/tag/v2.4.1).
- Pure-Python alternative: [`python/`](https://github.com/vogtpet/MSPAstandalone/tree/main/python).

### Citation

If you use MSPA in a publication, please cite:

- P. Soille and P. Vogt (2009): *Morphological segmentation of binary
  patterns*. Pattern Recognition Letters 30(4):456–459.
  https://doi.org/10.1016/j.patrec.2008.10.015
- P. Soille and P. Vogt (2022): *Morphological Spatial Pattern Analysis:
  open source release*. ISPRS Archives XLVIII-4/W1-2022, 427–433.
  https://doi.org/10.5194/isprs-archives-XLVIII-4/W1-2022-427-2022
