/*
 * tiffcmp - compare the pixel data of two MSPA output images.
 *
 * Only the pixel values are compared, not the TIFF metadata (which contains
 * a timestamp and therefore differs between runs). The tool uses libtiff,
 * which is already a build dependency of MSPA, so no Python/ImageMagick
 * installation is required.
 *
 * Usage: tiffcmp reference.tif candidate.tif
 *
 * Exit status: 0 = identical, 1 = different, 2 = error.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tiffio.h>

static int fail(const char *msg, const char *file)
{
  if (file != NULL)
    fprintf(stderr, "tiffcmp: %s: %s\n", msg, file);
  else
    fprintf(stderr, "tiffcmp: %s\n", msg);
  return 2;
}

int main(int argc, char **argv)
{
  TIFF *ref = NULL;
  TIFF *cand = NULL;
  uint32_t wref = 0, href = 0, wcand = 0, hcand = 0;
  uint16_t sref = 1, scand = 1;
  uint16_t bref = 0, bcand = 0;
  tmsize_t lref, lcand;
  unsigned char *rref = NULL, *rcand = NULL;
  uint32_t row;
  unsigned long long diff_rows = 0;
  int status = 0;

  if (argc != 3) {
    fprintf(stderr, "Usage: %s reference.tif candidate.tif\n", argv[0]);
    return 2;
  }

  ref = TIFFOpen(argv[1], "r");
  if (ref == NULL)
    return fail("cannot open", argv[1]);

  cand = TIFFOpen(argv[2], "r");
  if (cand == NULL) {
    status = fail("cannot open", argv[2]);
    goto done;
  }

  if (!TIFFGetField(ref, TIFFTAG_IMAGEWIDTH, &wref) ||
      !TIFFGetField(ref, TIFFTAG_IMAGELENGTH, &href) ||
      !TIFFGetField(cand, TIFFTAG_IMAGEWIDTH, &wcand) ||
      !TIFFGetField(cand, TIFFTAG_IMAGELENGTH, &hcand)) {
    status = fail("image has no size information", NULL);
    goto done;
  }

  /* Defaults apply when the tags are absent */
  TIFFGetField(ref, TIFFTAG_SAMPLESPERPIXEL, &sref);
  TIFFGetField(cand, TIFFTAG_SAMPLESPERPIXEL, &scand);
  TIFFGetField(ref, TIFFTAG_BITSPERSAMPLE, &bref);
  TIFFGetField(cand, TIFFTAG_BITSPERSAMPLE, &bcand);

  if ((wref != wcand) || (href != hcand)) {
    fprintf(stderr, "tiffcmp: size differs: %ux%u vs %ux%u\n",
            wref, href, wcand, hcand);
    status = 1;
    goto done;
  }

  if ((sref != scand) || (bref != bcand)) {
    fprintf(stderr,
            "tiffcmp: pixel layout differs: %u samples/%u bits vs "
            "%u samples/%u bits\n",
            sref, bref, scand, bcand);
    status = 1;
    goto done;
  }

  lref = TIFFScanlineSize(ref);
  lcand = TIFFScanlineSize(cand);
  if ((lref <= 0) || (lcand <= 0) || (lref != lcand)) {
    fprintf(stderr, "tiffcmp: scanline size differs: %ld vs %ld\n",
            (long)lref, (long)lcand);
    status = 1;
    goto done;
  }

  rref = (unsigned char *)_TIFFmalloc(lref);
  rcand = (unsigned char *)_TIFFmalloc(lcand);
  if ((rref == NULL) || (rcand == NULL)) {
    status = fail("out of memory", NULL);
    goto done;
  }

  for (row = 0; row < href; row++) {
    if ((TIFFReadScanline(ref, rref, row, 0) < 0) ||
        (TIFFReadScanline(cand, rcand, row, 0) < 0)) {
      fprintf(stderr, "tiffcmp: cannot read scanline %u\n", row);
      status = 2;
      goto done;
    }

    if (memcmp(rref, rcand, (size_t)lref) != 0) {
      diff_rows++;
      if (diff_rows == 1) {
        tmsize_t i;
        for (i = 0; i < lref; i++) {
          if (rref[i] != rcand[i])
            break;
        }
        fprintf(stderr,
                "tiffcmp: first difference at row %u, byte %ld "
                "(reference %u, candidate %u)\n",
                row, (long)i, (unsigned)rref[i], (unsigned)rcand[i]);
      }
    }
  }

  if (diff_rows != 0) {
    fprintf(stderr, "tiffcmp: FAIL: %llu of %u rows differ\n",
            diff_rows, href);
    status = 1;
  } else {
    printf("OK: %ux%u pixels are identical\n", wref, href);
  }

done:
  if (rref != NULL)
    _TIFFfree(rref);
  if (rcand != NULL)
    _TIFFfree(rcand);
  if (ref != NULL)
    TIFFClose(ref);
  if (cand != NULL)
    TIFFClose(cand);

  return status;
}
