# Test image and reference results

`input.tif` is a 1000 x 1000 binary test image. `deploy.sh` runs the freshly
built binary on this image twice:

```sh
./mspa_<target> -i input.tif -o output.tif -odir ./
./mspa_<target> -i input.tif -o output_nondefault.tif -odir ./ -graphfg 4 -internal 0
```

Both output files are written to this directory and are ignored by git.

## Comparing results between platforms

The TIFF writer stores a timestamp and the software version, so two runs are
not necessarily byte-identical even when the pixel data is identical. Compare
the pixel values instead, for example with GDAL:

```sh
gdalinfo -checksum output.tif
```

or with the helper script (requires Pillow):

```sh
python3 compare_outputs.py output_linux.tif output_macos_arm.tif
```

## Reference class counts

The following class counts were obtained from the Linux x86_64 build and are
identical for the macOS build. They can be used to verify a new build on any
platform. The pixel values are the MSPA class codes.

Default parameters (`-graphfg 8 -internal 1 -transition 1`):

```
0: 514124    1: 16602     3: 52863     9: 13917    17: 321747
33: 6085    35: 3384     65: 2265     67: 1701   100: 5927
101: 232    103: 38      105: 8628   109: 39     117: 20
129: 270    133: 20      135: 10     137: 10     165: 287
169: 642    220: 51189
```

`-graphfg 4 -internal 0`:

```
0: 571240    1: 15816     3: 51804     5: 8228     9: 18345
17: 321767  33: 4081     35: 4966     37: 14      65: 1205
67: 1648    69: 616     129: 270
```

Class codes: 1 = core, 3 = perforation, 5 = edge, 9 = loop, 17 = bridge,
33 = branch, 65 = islet, 129 = background, 220 = internal hole. Transition
classes are the same codes plus 34 (for example 35 = core transition).
