# Archived original C implementation (MSPA 2.4)

The original, pre-optimization C implementation is preserved at the immutable
Git tag **`c-baseline-v2.4`** (commit `ca9aa64`).

It is kept as the reference implementation for:

- reviewing the original algorithm and allocations;
- reproducing the parity baseline used to validate the optimized C code;
- building the original binary for A/B timing and memory comparisons.

The optimized implementation became the default source on `main` in release
`v2.4.2`.  Both implementations are MSPA **version 2.4** and produce
pixel-identical output.

## Build the archived baseline

Check out the tag and build as usual:

```sh
git clone https://github.com/vogtpet/MSPAstandalone.git
cd MSPAstandalone
git checkout c-baseline-v2.4
./source/deploy.sh Linux          # or Linux_ARM, macOS_ARM, Windows, Windows_ARM
```

The source tree, `deploy.sh`, makefiles and test images are complete at the
tag.  Build artifacts are not stored in Git; build them from the tag when
needed.

## Validation note

The optimized C implementation was checked against this baseline over 322
cases: edge widths 1–10, `graphfg` 4/8, `internal` 0/1, `transition` 0/1,
normal and `-disk` modes, and rasters from 1000×1000 up to 70000×128.  All
outputs were pixel-identical.  A 214,000×152,000 Turkey map was additionally
confirmed pixel-identical with the C reference in IDL.
