/***********************************************************************
Author(s): Pierre Soille
Copyright (C) 2000-2026 European Union (Joint Research Centre)

This file is part of miallib.

miallib is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

miallib is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with miallib.  If not, see <https://www.gnu.org/licenses/>.
***********************************************************************/

/* provides a distinct label to each connected component of a binary image
   it is ASSUMED that the input image has a zero valued frame accordingly to shft
   the first label is 2 */

				/* im: pointer to I/O image */
				/* nx: number of columns */
				/* ny: number of lines */
				/* nz: number of planes */
                                /* shft: array of shifts to access the neighbors */
				/* n: numbers of shifts */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miallib.h"
#include "fifo.h"


/** @defgroup group_label Connected component labelling
 *  Functions labelling image connected components based on pre-defined connectivity relations.
 *  The labelling of flat zones (labelplat function) is described in \cite soille2004sv
 *  @{
 */



#ifndef NO_generic_IMAGE
#include "g_def.h"
ERROR_TYPE generic_label(IMAGE *im1, IMAGE *im2, int ox, int oy, int oz)
{
  /*
  ** authors: P. Soille
  ** im1: Input and output image (destructive function)
  ** im2: UCHAR image defining neighbourhood (1 for a neighbour of origin)
  ** ox: x coordinate of origin
  ** oy: y coordinate of origin
  ** oz: z coordinate of origin

  ** comment:
  */

  long int  k, *shft, n;
  PIX_TYPE *p, *plast, *p1, *p2;
  PIX_TYPE lbl = 1;
  FIFO4 *q;
  long int nx, ny, nz;
  int box[BOXELEM];

  n = objectpix(im2);
  if (n==ERROR) /* no point in SE */
    return ERROR;
  shft = (long int *)calloc(n, sizeof(long int));
  if (shft == NULL)
    return ERROR;

  /*  Take SE  into account  */
  box[0] = GetImNx(im2);
  box[1] = GetImNy(im2);
  box[2] = GetImNz(im2);
  box[3] = ox;
  box[4] = oy;
  box[5] = oz;
  set_shift_and_box((UCHAR *)GetImPtr(im2), box, GetImNx(im1), GetImNy(im1), shft);
  if (generic_framebox(im1,box,0)==ERROR){
    free((char*)shft);
    return ERROR;
  }

  nx = GetImNx(im1);
  ny = GetImNy(im1);
  nz = GetImNz(im1);

  q = create_fifo4((nx*ny*nz)/100L);
  if (q == NULL){
    free((char*)shft);
    return ERROR;
  }
  /* Here we go */
  p   = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p == 1){
      if (lbl==PIX_MAX){
	(void)sprintf(buf,"label(): warning: there are more than %d connected \
                      components, the remaining components have been labeled \
                      starting again with label value 2!.\n", PIX_MAX);
	errputstr(buf);
	/* should add a flag to either stop the labelling with break or reset lbl */
	/*break; modified on 2003-06-20 crystal clear view on Weissmies */
	lbl=1;
      }
      *p = ++lbl;
      fifo4_add(q, (intptr_t)p);
      while ((p1 = (PIX_TYPE *)fifo4_remove(q))){
	for (k=0; k < n; k++){
          p2 = p1 + shft[k];
	  if (*p2 == 1){
	    *p2 = lbl;
	    fifo4_add(q, (intptr_t)p2);
	  }
	}
      }
    }
  }
  free((char*)shft);
  free_fifo4(q);
  return NO_ERROR;
}
#include "g_undef.h"
#endif /* #ifndef NO_generic_IMAGE */


#include "u32_def.h"
#include <stdint.h> /* Required for uintptr_t */

/* ------------------------------------------------------------------ */
/* Fast 4/8-connected labelling for the 3x3 structuring elements used  */
/* by MSPA (label() is also used with arbitrary SEs, so those keep the  */
/* queue implementation below).  The result is the same partition;     */
/* only the (arbitrary) label numbers may differ.                      */
/* ------------------------------------------------------------------ */

typedef struct {
  INT64 start;
  INT64 end;             /* inclusive */
  UINT32 label;
} U32Run;

static UINT32 u32_uf_find(UINT32 *parent, UINT32 x)
{
  UINT32 r = x;
  while (parent[r] != r)
    r = parent[r];
  while (parent[x] != x){
    UINT32 next = parent[x];
    parent[x] = r;
    x = next;
  }
  return r;
}

static void u32_uf_union(UINT32 *parent, unsigned char *rank,
                         UINT32 a, UINT32 b)
{
  UINT32 ra = u32_uf_find(parent, a);
  UINT32 rb = u32_uf_find(parent, b);
  if (ra == rb)
    return;
  if (rank[ra] < rank[rb]){
    parent[ra] = rb;
  }
  else if (rank[ra] > rank[rb]){
    parent[rb] = ra;
  }
  else{
    parent[rb] = ra;
    rank[ra]++;
  }
}

/* Returns 4 or 8 for the two MSPA SEs, 0 for any other SE. */
static int u32_label_fast_se(IMAGE *im2, int ox, int oy, int oz)
{
  UCHAR *se;
  int corners;

  if (GetImDataType(im2) != t_UCHAR ||
      GetImNx(im2) != 3 || GetImNy(im2) != 3 || GetImNz(im2) != 1 ||
      ox != 1 || oy != 1 || oz != 0)
    return 0;
  se = (UCHAR *)GetImPtr(im2);
  if (!se[4] || !se[1] || !se[3] || !se[5] || !se[7])
    return 0;
  corners = (se[0] != 0) + (se[2] != 0) + (se[6] != 0) + (se[8] != 0);
  if (corners == 4)
    return 8;
  if (corners == 0)
    return 4;
  return 0;
}

/*
 * Run-length two-pass connected components labelling.
 *
 * The image is modified in place: pixels equal to 1 receive provisional
 * labels, then contiguous final labels starting at 2.  This mirrors the
 * numbering convention of the queue implementation.  Values other than 0
 * and 1 make the routine return 2 so the caller can use the generic path.
 */
static ERROR_TYPE u32_label_runs(IMAGE *im1, int conn)
{
  size_t nx = (size_t)GetImNx(im1);
  size_t ny = (size_t)GetImNy(im1);
  size_t npix = nx * ny;
  UINT32 *p = (UINT32 *)GetImPtr(im1);
  U32Run *prev, *cur, *tmp;
  UINT32 *parent, *rootmap, *final;
  unsigned char *rank;
  UINT32 pcap = 1024, next = 2, lab, outlab;
  size_t rcap, i, x, y, pn, cn, k, j, pj;
  int box[6] = {1, 1, 1, 1, 0, 0};

  if (npix == 0)
    return NO_ERROR;
  if (u32_framebox(im1, box, 0) == ERROR)
    return ERROR;

  /* The generic label path only labels pixels equal to 1. */
  for (i = 0; i < npix; i++){
    if (p[i] > 1)
      return 2;
  }

  rcap = (nx + 1) / 2 + 2;
  prev = (U32Run *)malloc(rcap * sizeof(U32Run));
  cur  = (U32Run *)malloc(rcap * sizeof(U32Run));
  parent = (UINT32 *)malloc((size_t)pcap * sizeof(UINT32));
  rank   = (unsigned char *)malloc((size_t)pcap);
  if (prev == NULL || cur == NULL || parent == NULL || rank == NULL){
    free(prev); free(cur); free(parent); free(rank);
    return ERROR;
  }

  pn = 0;
  for (y = 0; y < ny; y++){
    size_t off = y * nx;
    cn = 0;
    pj = 0;
    x = 0;
    while (x < nx){
      INT64 s, e;
      if (p[off + x] != 1){
        x++;
        continue;
      }
      s = (INT64)x;
      while (x < nx && p[off + x] == 1)
        x++;
      e = (INT64)x - 1;

      if (next == UINT32_MAX){
        free(prev); free(cur); free(parent); free(rank);
        return ERROR;
      }
      if (next >= pcap){
        UINT32 ncap = pcap * 2;
        UINT32 *np;
        unsigned char *nr;
        if (ncap <= pcap){
          free(prev); free(cur); free(parent); free(rank);
          return ERROR;
        }
        np = (UINT32 *)realloc(parent, (size_t)ncap * sizeof(UINT32));
        if (np == NULL){
          free(prev); free(cur); free(parent); free(rank);
          return ERROR;
        }
        parent = np;
        nr = (unsigned char *)realloc(rank, (size_t)ncap);
        if (nr == NULL){
          free(prev); free(cur); free(parent); free(rank);
          return ERROR;
        }
        rank = nr;
        pcap = ncap;
      }
      lab = next++;
      parent[lab] = lab;
      rank[lab] = 0;

      for (k = (size_t)s; k <= (size_t)e; k++)
        p[off + k] = lab;

      /* Previous-row runs are sorted; advance past those that end before
       * this run can connect. */
      while (pj < pn){
        if (conn == 4){
          if (prev[pj].end >= s)
            break;
        }
        else{
          if (prev[pj].end + 1 >= s)
            break;
        }
        pj++;
      }
      for (j = pj; j < pn; j++){
        int connected;
        if (conn == 4)
          connected = (prev[j].start <= e && prev[j].end >= s);
        else
          connected = (prev[j].start <= e + 1 && prev[j].end + 1 >= s);
        if (connected)
          u32_uf_union(parent, rank, lab, prev[j].label);
        else if ((conn == 4 && prev[j].start > e) ||
                 (conn == 8 && prev[j].start > e + 1))
          break;
      }

      cur[cn].start = s;
      cur[cn].end = e;
      cur[cn].label = lab;
      cn++;
    }
    tmp = prev; prev = cur; cur = tmp;
    pn = cn;
  }

  rootmap = (UINT32 *)calloc(next, sizeof(UINT32));
  final   = (UINT32 *)malloc((size_t)next * sizeof(UINT32));
  if (rootmap == NULL || final == NULL){
    free(prev); free(cur); free(parent); free(rank);
    free(rootmap); free(final);
    return ERROR;
  }
  outlab = 2;
  for (lab = 2; lab < next; lab++){
    UINT32 r = u32_uf_find(parent, lab);
    if (rootmap[r] == 0)
      rootmap[r] = outlab++;
    final[lab] = rootmap[r];
  }
  for (i = 0; i < npix; i++){
    UINT32 v = p[i];
    if (v >= 2)
      p[i] = final[v];
  }

  free(prev); free(cur); free(parent); free(rank);
  free(rootmap); free(final);
  return NO_ERROR;
}

ERROR_TYPE u32_label(IMAGE *im1, IMAGE *im2, int ox, int oy, int oz)
{
  long int  k, *shft, n;
  PIX_TYPE *p, *plast, *p1, *p2;
  PIX_TYPE lbl = 1;
  FIFO4 *q;
  size_t nx, ny, nz; /* FIXED: Changed to size_t to prevent 32-bit integer overflow */
  int box[BOXELEM];
  int fast_se = u32_label_fast_se(im2, ox, oy, oz);

  if (fast_se){
    ERROR_TYPE st = u32_label_runs(im1, fast_se);
    if (st != 2)
      return st;
  }

  n = objectpix(im2);
  if (n==ERROR)
    return ERROR;
  shft = (long int *)calloc(n, sizeof(long int));
  if (shft == NULL)
    return ERROR;

  /*  Take SE  into account  */
  box[0] = GetImNx(im2);
  box[1] = GetImNy(im2);
  box[2] = GetImNz(im2);
  box[3] = ox;
  box[4] = oy;
  box[5] = oz;
  set_shift_and_box((UCHAR *)GetImPtr(im2), box, GetImNx(im1), GetImNy(im1), shft);
  if (u32_framebox(im1,box,0)==ERROR){
    free((char*)shft);
    return ERROR;
  }

  /* FIXED: Store dimensions as size_t */
  nx = (size_t)GetImNx(im1);
  ny = (size_t)GetImNy(im1);
  nz = (size_t)GetImNz(im1);

  /* FIXED: Enforce 64-bit evaluation for total pixels calculation */
  q = create_fifo4((long int)((nx * ny * nz) / 100ULL));
  if (q == NULL){
    free((char*)shft);
    return ERROR;
  }
  /* Here we go */
  p   = (PIX_TYPE *)GetImPtr(im1);
  plast = p + (nx * ny * nz); /* FIXED: Pointer math now steps cleanly using 64-bit bounds */
  for (; p < plast; p++){
    if (*p == 1){
      if (lbl==PIX_MAX){
	(void)sprintf(buf,"label(): warning: there are more than %d connected components, the remaining components have been labeled starting again with label value 2!.\n", PIX_MAX); errputstr(buf);
	/* should add a flag to either stop the labelling with break or reset lbl */
	/*break;*/
	lbl=1;
      }
      *p = ++lbl;

      /* FIXED: Cast pointer to uintptr_t (64-bit) then to long int to preserve the full pointer address */
      fifo4_add(q, (intptr_t)(uintptr_t)p);
      while ((p1 = (PIX_TYPE *)fifo4_remove(q))){
	    for (k=0; k < n; k++){
          p2 = p1 + shft[k];
	      if (*p2 == 1){
	       *p2 = lbl;
           /* FIXED: Preserves the 64-bit structural address during queueing */
	       fifo4_add(q, (intptr_t)(uintptr_t)p2);
	  }
	}
      }
    }
  }
  free((char*)shft);
  free_fifo4(q);
  return NO_ERROR;
}
#include "u32_undef.h"

#include "i32_def.h"
ERROR_TYPE i32_label(IMAGE *im1, IMAGE *im2, int ox, int oy, int oz)
{
  long int  k, *shft, n;
  PIX_TYPE *p, *plast, *p1, *p2;
  PIX_TYPE lbl = 1;
  FIFO4 *q;
  long int nx, ny, nz;
  int box[BOXELEM];

  n = objectpix(im2);
  if (n==ERROR)
    return ERROR;
  shft = (long int *)calloc(n, sizeof(long int));
  if (shft == NULL)
    return ERROR;

  /*  Take SE  into account  */
  box[0] = GetImNx(im2);
  box[1] = GetImNy(im2);
  box[2] = GetImNz(im2);
  box[3] = ox;
  box[4] = oy;
  box[5] = oz;
  set_shift_and_box((UCHAR *)GetImPtr(im2), box, GetImNx(im1), GetImNy(im1), shft);
  if (i32_framebox(im1,box,0)==ERROR){
	 free((char*)shft);
    return ERROR;
  }

  nx = GetImNx(im1);
  ny = GetImNy(im1);
  nz = GetImNz(im1);

  q = create_fifo4((nx*ny*nz)/100L);
  if (q == NULL){
    free((char*)shft);
    return ERROR;
  }
  /* Here we go */
  p   = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p == 1){
      if (lbl==PIX_MAX){
	(void)sprintf(buf,"label(): warning: there are more than %d connected components, the remaining components have been labeled starting again with label value 2!.\n", PIX_MAX); errputstr(buf);
	/* should add a flag to either stop the labelling with break or reset lbl */
	/*break;*/
	lbl=1;
      }
      *p = ++lbl;
      fifo4_add(q, (intptr_t)p);
      while ((p1 = (PIX_TYPE *)fifo4_remove(q))){
	for (k=0; k < n; k++){
          p2 = p1 + shft[k];
	  if (*p2 == 1){
	    *p2 = lbl;
	    fifo4_add(q, (intptr_t)p2);
	  }
	}
      }
    }
  }
  free((char*)shft);
  free_fifo4(q);
  return NO_ERROR;
}
#include "i32_undef.h"

#include "us_def.h"
ERROR_TYPE us_label(IMAGE *im1, IMAGE *im2, int ox, int oy, int oz)
{
  long int  k, *shft, n;
  PIX_TYPE *p, *plast, *p1, *p2;
  PIX_TYPE lbl = 1;
  FIFO4 *q;
  long int nx, ny, nz;
  int box[BOXELEM];

  n = objectpix(im2);
  if (n==ERROR)
    return ERROR;
  shft = (long int *)calloc(n, sizeof(long int));
  if (shft == NULL)
    return ERROR;

  /*  Take SE  into account  */
  box[0] = GetImNx(im2);
  box[1] = GetImNy(im2);
  box[2] = GetImNz(im2);
  box[3] = ox;
  box[4] = oy;
  box[5] = oz;
  set_shift_and_box((UCHAR *)GetImPtr(im2), box, GetImNx(im1), GetImNy(im1), shft);

  if (us_framebox(im1,box,0)==ERROR){
    free((char*)shft);
    return ERROR;
  }

  nx = GetImNx(im1);
  ny = GetImNy(im1);
  nz = GetImNz(im1);

  q = create_fifo4((nx*ny*nz)/100L);
  if (q == NULL){
    free((char*)shft);
    return ERROR;
  }
  /* Here we go */
  p   = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p == 1){
      if (lbl==PIX_MAX){
	(void)sprintf(buf,"label(): warning: there are more than %d connected components, the remaining components have been labeled starting again with label value 2!.\n", PIX_MAX); errputstr(buf);
	/* should add a flag to either stop the labelling with break or reset lbl */
	/*break;*/
	lbl=1;
      }
      *p = ++lbl;
      fifo4_add(q, (intptr_t)p);
      while ((p1 = (PIX_TYPE *)fifo4_remove(q))){
	for (k=0; k < n; k++){
          p2 = p1 + shft[k];
	  if (*p2 == 1){
	    *p2 = lbl;
	    fifo4_add(q, (intptr_t)p2);
	  }
	}
      }
    }
  }
  free((char*)shft);
  free_fifo4(q);
  return NO_ERROR;
}
#include "us_undef.h"


ERROR_TYPE label(IMAGE *im1, IMAGE *im2, int ox, int oy, int oz)
{
  /* im1: input image
     im2: for defining neighbourhgood
  */
  switch (GetImDataType(im1)){

#ifndef NO_generic_IMAGE
  case t_GENERIC:
	 return(generic_label(im1,im2,ox,oy,oz));
    break;
#endif

#ifndef NO_uc_IMAGE
  case t_UCHAR:
    return(uc_label(im1,im2,ox,oy,oz));
    break;
#endif

  case t_USHORT:
    return(us_label(im1,im2,ox,oy,oz));
    break;

  case t_INT32:
    return(i32_label(im1,im2,ox,oy,oz));
    break;

  case t_UINT32:
    return(u32_label(im1,im2,ox,oy,oz));
    break;

  default:
    (void)sprintf(buf,"label(): invalid pixel type\n"); errputstr(buf);
    return(ERROR);
  }
  return(NO_ERROR);
}

#include "us_def.h"
ERROR_TYPE us_labelpixngb(IMAGE *im1, IMAGE *im2, int ox, int oy, int oz)
{
  long int  k, *shft, n;
  PIX_TYPE *p, *plast, *p1, *p2;
  PIX_TYPE lbl = 1; /* first label with value 1 */
  FIFO4 *q;
  long int nx, ny, nz;
  int box[BOXELEM];

  n = objectpix(im2);
  if (n==ERROR)
    return ERROR;
  shft = (long int *)calloc(n, sizeof(long int));
  if (shft == NULL)
    return ERROR;

  /*  Take SE  into account  */
  box[0] = GetImNx(im2);
  box[1] = GetImNy(im2);
  box[2] = GetImNz(im2);
  box[3] = ox;
  box[4] = oy;
  box[5] = oz;
  set_shift_and_box((UCHAR *)GetImPtr(im2), box, GetImNx(im1), GetImNy(im1), shft);
  if (i32_framebox(im1,box,0)==ERROR){
	 free((char*)shft);
    return ERROR;
  }

  nx = GetImNx(im1);
  ny = GetImNy(im1);
  nz = GetImNz(im1);

  q = create_fifo4((nx*ny*nz)/100L);
  if (q == NULL){
    free((char*)shft);
    return ERROR;
  }
  /* Here we go */
  p   = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p == 1){
      if (lbl==PIX_MAX){
	(void)sprintf(buf,"label(): warning: there are more than %d connected components, the remaining components have been labeled starting again with label value 2!.\n", PIX_MAX); errputstr(buf);
	/* should add a flag to either stop the labelling with break or reset lbl */
	/*break;*/
	lbl=1;
      }
      *p = ++lbl;
      fifo4_add(q, (intptr_t)p);
      while ((p1 = (PIX_TYPE *)fifo4_remove(q))){
	for (k=0; k < n; k++){
          p2 = p1 + shft[k];
	  if (*p2 == 1){
	    *p2 = ++lbl;
	    fifo4_add(q, (intptr_t)p2);
	  }
	}
      }
    }
  }
  free((char*)shft);
  free_fifo4(q);
  return NO_ERROR;
}
#include "us_undef.h"


#include "i32_def.h"
ERROR_TYPE i32_labelpixngb(IMAGE *im1, IMAGE *im2, int ox, int oy, int oz)
{
  long int  k, *shft, n;
  PIX_TYPE *p, *plast, *p1, *p2;
  PIX_TYPE lbl = 1;
  FIFO4 *q;
  long int nx, ny, nz;
  int box[BOXELEM];

  n = objectpix(im2);
  if (n==ERROR)
    return ERROR;
  shft = (long int *)calloc(n, sizeof(long int));
  if (shft == NULL)
    return ERROR;

  /*  Take SE  into account  */
  box[0] = GetImNx(im2);
  box[1] = GetImNy(im2);
  box[2] = GetImNz(im2);
  box[3] = ox;
  box[4] = oy;
  box[5] = oz;
  set_shift_and_box((UCHAR *)GetImPtr(im2), box, GetImNx(im1), GetImNy(im1), shft);
  if (i32_framebox(im1,box,0)==ERROR){
	 free((char*)shft);
    return ERROR;
  }

  nx = GetImNx(im1);
  ny = GetImNy(im1);
  nz = GetImNz(im1);

  q = create_fifo4((nx*ny*nz)/100L);
  if (q == NULL){
    free((char*)shft);
    return ERROR;
  }
  /* Here we go */
  p   = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p == 1){
      if (lbl==PIX_MAX){
	(void)sprintf(buf,"label(): warning: there are more than %d connected components, the remaining components have been labeled starting again with label value 2!.\n", PIX_MAX); errputstr(buf);
	/* should add a flag to either stop the labelling with break or reset lbl */
	/*break;*/
	lbl=1;
      }
      *p = ++lbl;
      fifo4_add(q, (intptr_t)p);
      while ((p1 = (PIX_TYPE *)fifo4_remove(q))){
	for (k=0; k < n; k++){
          p2 = p1 + shft[k];
	  if (*p2 == 1){
	    *p2 = ++lbl;
	    fifo4_add(q, (intptr_t)p2);
	  }
	}
      }
    }
  }
  free((char*)shft);
  free_fifo4(q);
  return NO_ERROR;
}
#include "i32_undef.h"


ERROR_TYPE labelpixngb(IMAGE *im1, IMAGE *im2, int ox, int oy, int oz)
{
  /* im1: input image
     im2: for defining neighbourhgood
  */
  switch (GetImDataType(im1)){
  case t_USHORT:
    return(us_labelpixngb(im1,im2,ox,oy,oz));
    break;
  case t_INT32:
    return(i32_labelpixngb(im1,im2,ox,oy,oz));
    break;
  default:
    (void)sprintf(buf,"label(): invalid pixel type\n"); errputstr(buf);
    return(ERROR);
  }
  return(NO_ERROR);
}

#ifndef NO_generic_IMAGE
#include "g_def.h"
ERROR_TYPE generic_labelplat(IMAGE *im1, IMAGE *im2, int ox, int oy, int oz)
{
  long int  k, *shft, n;
  PIX_TYPE *p, *plast, *p1, *p2;
  PIX_TYPE lbl = 1, val;
  FIFO4 *q;
  long int nx, ny, nz;
  int box[BOXELEM];

  n = objectpix(im2);
  if (n==ERROR)
    return ERROR;
  shft = (long int *)calloc(n, sizeof(long int));
  if (shft == NULL)
    return ERROR;

  /*  Take SE  into account  */
  box[0] = GetImNx(im2);
  box[1] = GetImNy(im2);
  box[2] = GetImNz(im2);
  box[3] = ox;
  box[4] = oy;
  box[5] = oz;
  set_shift_and_box((UCHAR *)GetImPtr(im2), box, GetImNx(im1), GetImNy(im1), shft);

  if (generic_framebox(im1,box,0)==ERROR){
    free((char*)shft);
    return ERROR;
  }

  nx = GetImNx(im1);
  ny = GetImNy(im1);
  nz = GetImNz(im1);


  p   = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p)
      *p |= PIX_MSB;
  }

  q = create_fifo4((nx*ny*nz)/100L);
  if (q == NULL){
    free((char*)shft);
    return ERROR;
  }
  /* Here we go */
  p   = (PIX_TYPE *)GetImPtr(im1);
  for (; p < plast; p++){
    if (*p > PIX_MSB){
      val = *p;
      if (lbl==PIX_MAX){
	(void)sprintf(buf,"labelplat(): there are more than %d connected components, the remaining components have not been labeled.\n", PIX_MAX); errputstr(buf);
	break;
      }
      *p = ++lbl;
      fifo4_add(q, (intptr_t)p);
      while ((p1 = (PIX_TYPE *)fifo4_remove(q))){
	for (k=0; k < n; k++){
          p2 = p1 + shft[k];
	  if (*p2 == val){
	    *p2 = lbl;
	    fifo4_add(q, (intptr_t)p2);
	  }
	}
      }
    }
  }
  free((char*)shft);
  free_fifo4(q);
  return NO_ERROR;
}
#include "g_undef.h"
#endif /* #ifndef NO_generic_IMAGE */

#include "us_def.h"
ERROR_TYPE us_labelplat(IMAGE *im1, IMAGE *im2, int ox, int oy, int oz)
{
  long int  k, *shft, n;
  PIX_TYPE *p, *plast, *p1, *p2;
  PIX_TYPE lbl = 1, val;
  FIFO4 *q;
  long int nx, ny, nz;
  int box[BOXELEM];

  n = objectpix(im2);
  if (n==ERROR)
    return ERROR;
  shft = (long int *)calloc(n, sizeof(long int));
  if (shft == NULL)
    return ERROR;

  /*  Take SE  into account  */
  box[0] = GetImNx(im2);
  box[1] = GetImNy(im2);
  box[2] = GetImNz(im2);
  box[3] = ox;
  box[4] = oy;
  box[5] = oz;
  set_shift_and_box((UCHAR *)GetImPtr(im2), box, GetImNx(im1), GetImNy(im1), shft);

  if (us_framebox(im1,box,0)==ERROR){
    free((char*)shft);
    return ERROR;
  }

  nx = GetImNx(im1);
  ny = GetImNy(im1);
  nz = GetImNz(im1);


  p   = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p)
      *p |= PIX_MSB;
  }

  q = create_fifo4((nx*ny*nz)/100L);
  if (q == NULL){
    free((char*)shft);
    return ERROR;
  }
  /* Here we go */
  p   = (PIX_TYPE *)GetImPtr(im1);
  for (; p < plast; p++){
    if (*p > PIX_MSB){
      val = *p;
      if (lbl==PIX_MAX){
	(void)sprintf(buf,"labelplat(): there are more than %d connected components, the remaining components have not been labeled.\n", PIX_MAX); errputstr(buf);
	break;
      }
      *p = ++lbl;
      fifo4_add(q, (intptr_t)p);
      while ((p1 = (PIX_TYPE *)fifo4_remove(q))){
	for (k=0; k < n; k++){
          p2 = p1 + shft[k];
	  if (*p2 == val){
	    *p2 = lbl;
	    fifo4_add(q, (intptr_t)p2);
	  }
	}
      }
    }
  }
  free((char*)shft);
  free_fifo4(q);
  return NO_ERROR;
}
#include "us_undef.h"


#include "i32_def.h"
ERROR_TYPE i32_labelplat(IMAGE *im1, IMAGE *im2, int ox, int oy, int oz)
{
  long int  k, *shft, n;
  PIX_TYPE *p, *plast, *p1, *p2;
  PIX_TYPE lbl = 1, val;
  FIFO4 *q;
  long int nx, ny, nz;
  int box[BOXELEM];

  n = objectpix(im2);
  if (n==ERROR)
    return ERROR;
  shft = (long int *)calloc(n, sizeof(long int));
  if (shft == NULL)
    return ERROR;

  /*  Take SE  into account  */
  box[0] = GetImNx(im2);
  box[1] = GetImNy(im2);
  box[2] = GetImNz(im2);
  box[3] = ox;
  box[4] = oy;
  box[5] = oz;
  set_shift_and_box((UCHAR *)GetImPtr(im2), box, GetImNx(im1), GetImNy(im1), shft);

  if (i32_framebox(im1,box,0)==ERROR){
    free((char*)shft);
    return ERROR;
  }

  nx = GetImNx(im1);
  ny = GetImNy(im1);
  nz = GetImNz(im1);


  p   = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p)
      *p |= PIX_MSB;
  }

  q = create_fifo4((nx*ny*nz)/100L);
  if (q == NULL){
    free((char*)shft);
    return ERROR;
  }
  /* Here we go */
  p   = (PIX_TYPE *)GetImPtr(im1);
  for (; p < plast; p++){
    if (*p > PIX_MSB){
      val = *p;
      if (lbl==PIX_MAX){
	(void)sprintf(buf,"labelplat(): there are more than %d connected components, the remaining components have not been labeled.\n", PIX_MAX); errputstr(buf);
	break;
      }
      *p = ++lbl;
      fifo4_add(q, (intptr_t)p);
      while ((p1 = (PIX_TYPE *)fifo4_remove(q))){
	for (k=0; k < n; k++){
          p2 = p1 + shft[k];
	  if (*p2 == val){
	    *p2 = lbl;
	    fifo4_add(q, (intptr_t)p2);
	  }
	}
      }
    }
  }
  free((char*)shft);
  free_fifo4(q);
  return NO_ERROR;
}
#include "i32_undef.h"



/* labelplat function is described in \cite soille2004sv */
ERROR_TYPE labelplat(IMAGE *im1, IMAGE *im2, int ox, int oy, int oz)
{
  switch (GetImDataType(im1)){

#ifndef NO_generic_IMAGE
  case t_GENERIC:
    return(generic_labelplat(im1,im2,ox,oy,oz));
    break;
#endif

#ifndef NO_uc_IMAGE
  case t_UCHAR:
    return(uc_labelplat(im1,im2,ox,oy,oz));
    break;
#endif

  case t_USHORT:
    return(us_labelplat(im1,im2,ox,oy,oz));
    break;

  case t_INT32:
  case t_UINT32:
    return(i32_labelplat(im1,im2,ox,oy,oz));
    break;

  default:
    (void)sprintf(buf,"labelplat(): invalid pixel type\n"); errputstr(buf);
    return(ERROR);
  }
  return(NO_ERROR);
}




#ifndef NO_generic_IMAGE
#include "g_def.h"
ERROR_TYPE generic_seededlabelplat(IMAGE *im1, IMAGE *im2, IMAGE *im3, int ox, int oy, int oz)
{
  long int  k, *shft, n;
  PIX_TYPE *p, *plast, *p1, *p2;
  PIX_TYPE lbl = 1, val;
  UCHAR *p3; /* image of seeds for flat regions */
  FIFO4 *q;
  int nx, ny, nz;
  int box[BOXELEM];

  n = objectpix(im2);
  if (n==ERROR)
    return ERROR;
  shft = (long int *)calloc(n, sizeof(long int));
  if (shft == NULL)
    return ERROR;

  /*  Take SE  into account  */
  box[0] = GetImNx(im2);
  box[1] = GetImNy(im2);
  box[2] = GetImNz(im2);
  box[3] = ox;
  box[4] = oy;
  box[5] = oz;
  set_shift_and_box((UCHAR *)GetImPtr(im2), box, GetImNx(im1), GetImNy(im1), shft);

  if (generic_framebox(im1,box,0)==ERROR){
    free((char*)shft);
    return ERROR;
  }

  nx = GetImNx(im1);
  ny = GetImNy(im1);
  nz = GetImNz(im1);


  p   = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p) /* shouldn't it be >= ???  for all (see below) */
      *p |= PIX_MSB;
  }

  q = create_fifo4((nx*ny*nz)/100L);
  if (q == NULL){
    free((char*)shft);
    return ERROR;
  }
  /* Here we go */
  p   = (PIX_TYPE *)GetImPtr(im1);
  p3  = (UCHAR *)GetImPtr(im3);
  for (; p < plast; p++, p3++){
    if (*p > PIX_MSB && *p3){  /* shouldn't it be >= ??? */
      val = *p;
      if (lbl==PIX_MAX){
	(void)sprintf(buf,"label(): there are more than %d connected components, the remaining components have not been labeled.\n", PIX_MAX); errputstr(buf);
	break;
      }
      *p = ++lbl;
      fifo4_add(q, (intptr_t)p);
      while ((p1 = (PIX_TYPE *)fifo4_remove(q))){
	for (k=0; k < n; k++){
          p2 = p1 + shft[k];
	  if (*p2 == val){
	    *p2 = lbl;
	    fifo4_add(q, (intptr_t)p2);
	  }
	}
      }
    }
  }
  p   = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p>=PIX_MSB) /* shouldn't it be >= ???  for all (see below) */
      *p = 0;
  }
  free((char*)shft);
  free_fifo4(q);
  return NO_ERROR;
}
#include "g_undef.h"
#endif /* #ifndef NO_generic_IMAGE */


#include "us_def.h"
ERROR_TYPE us_seededlabelplat(IMAGE *im1, IMAGE *im2, IMAGE *im3, int ox, int oy, int oz)
{
  long int  k, *shft, n;
  PIX_TYPE *p, *plast, *p1, *p2;
  PIX_TYPE lbl = 1, val;
  UCHAR *p3; /* image of seeds for flat regions */
  FIFO4 *q;
  int nx, ny, nz;
  int box[BOXELEM];

  n = objectpix(im2);
  if (n==ERROR)
    return ERROR;
  shft = (long int *)calloc(n, sizeof(long int));
  if (shft == NULL)
    return ERROR;

  /*  Take SE  into account  */
  box[0] = GetImNx(im2);
  box[1] = GetImNy(im2);
  box[2] = GetImNz(im2);
  box[3] = ox;
  box[4] = oy;
  box[5] = oz;
  set_shift_and_box((UCHAR *)GetImPtr(im2), box, GetImNx(im1), GetImNy(im1), shft);

  if (us_framebox(im1,box,0)==ERROR){
    free((char*)shft);
    return ERROR;
  }

  nx = GetImNx(im1);
  ny = GetImNy(im1);
  nz = GetImNz(im1);


  p   = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p) /* shouldn't it be >= ???  for all (see below) */
      *p |= PIX_MSB;
  }

  q = create_fifo4((nx*ny*nz)/100L);
  if (q == NULL){
    free((char*)shft);
    return ERROR;
  }
  /* Here we go */
  p   = (PIX_TYPE *)GetImPtr(im1);
  p3  = (UCHAR *)GetImPtr(im3);
  for (; p < plast; p++, p3++){
    if (*p > PIX_MSB && *p3){  /* shouldn't it be >= ??? */
      val = *p;
      if (lbl==PIX_MAX){
	(void)sprintf(buf,"label(): there are more than %d connected components, the remaining components have not been labeled.\n", PIX_MAX); errputstr(buf);
	break;
      }
      *p = ++lbl;
      fifo4_add(q, (intptr_t)p);
      while ((p1 = (PIX_TYPE *)fifo4_remove(q))){
	for (k=0; k < n; k++){
          p2 = p1 + shft[k];
	  if (*p2 == val){
	    *p2 = lbl;
	    fifo4_add(q, (intptr_t)p2);
	  }
	}
      }
    }
  }
  p   = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p>=PIX_MSB) /* shouldn't it be >= ???  for all (see below) */
      *p = 0;
  }
  free((char*)shft);
  free_fifo4(q);
  return NO_ERROR;
}
#include "us_undef.h"


#include "u32_def.h"
ERROR_TYPE u32_seededlabelplat(IMAGE *im1, IMAGE *im2, IMAGE *im3, int ox, int oy, int oz)
{
  long int  k, *shft, n;
  PIX_TYPE *p, *plast, *p1, *p2;
  PIX_TYPE lbl = 1, val;
  UCHAR *p3; /* image of seeds for flat regions */
  FIFO4 *q;
  int nx, ny, nz;
  int box[BOXELEM];

  n = objectpix(im2);
  if (n==ERROR)
    return ERROR;
  shft = (long int *)calloc(n, sizeof(long int));
  if (shft == NULL)
    return ERROR;

  /*  Take SE  into account  */
  box[0] = GetImNx(im2);
  box[1] = GetImNy(im2);
  box[2] = GetImNz(im2);
  box[3] = ox;
  box[4] = oy;
  box[5] = oz;
  set_shift_and_box((UCHAR *)GetImPtr(im2), box, GetImNx(im1), GetImNy(im1), shft);

  if (u32_framebox(im1,box,0)==ERROR){
    free((char*)shft);
    return ERROR;
  }

  nx = GetImNx(im1);
  ny = GetImNy(im1);
  nz = GetImNz(im1);


  p   = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p) /* shouldn't it be >= ???  for all (see below) */
      *p |= PIX_MSB;
  }

  q = create_fifo4((nx*ny*nz)/100L);
  if (q == NULL){
    free((char*)shft);
    return ERROR;
  }
  /* Here we go */
  p   = (PIX_TYPE *)GetImPtr(im1);
  p3  = (UCHAR *)GetImPtr(im3);
  for (; p < plast; p++, p3++){
    if (*p > PIX_MSB && *p3){  /* shouldn't it be >= ??? */
      val = *p;
      if (lbl==PIX_MAX){
	(void)sprintf(buf,"label(): there are more than %d connected components, the remaining components have not been labeled.\n", PIX_MAX); errputstr(buf);
	break;
      }
      *p = ++lbl;
      fifo4_add(q, (intptr_t)p);
      while ((p1 = (PIX_TYPE *)fifo4_remove(q))){
	for (k=0; k < n; k++){
          p2 = p1 + shft[k];
	  if (*p2 == val){
	    *p2 = lbl;
	    fifo4_add(q, (intptr_t)p2);
	  }
	}
      }
    }
  }
  p   = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p>=PIX_MSB) /* shouldn't it be >= ???  for all (see below) */
      *p = 0;
  }
  free((char*)shft);
  free_fifo4(q);
  return NO_ERROR;
}
#include "u32_undef.h"


ERROR_TYPE seededlabelplat(IMAGE *im1, IMAGE *im2, IMAGE *im3, int ox, int oy, int oz)
{

  switch (GetImDataType(im1)){

#ifndef NO_generic_IMAGE
  case t_GENERIC:
    return(generic_seededlabelplat(im1,im2,im3,ox,oy,oz));
    break;
#endif

#ifndef NO_uc_IMAGE
  case t_UCHAR:
    return(uc_seededlabelplat(im1,im2,im3,ox,oy,oz));
    break;
#endif

  case t_USHORT:
    return(us_seededlabelplat(im1,im2,im3,ox,oy,oz));
    break;

  case t_UINT32:
    return(u32_seededlabelplat(im1,im2,im3,ox,oy,oz));
    break;

  default:
    (void)sprintf(buf,"seededlabelplat(): invalid pixel type\n"); errputstr(buf);
    return(ERROR);
  }
  return(NO_ERROR);
}






#ifndef NO_generic_IMAGE
#include "g_def.h"
ERROR_TYPE generic_seededplat(IMAGE *im1, IMAGE *im2, IMAGE *im3, int ox, int oy, int oz)
{
  /* each flat region marked by a seed is set to 1
     we assume that PIX_MSB and PIX_MSB-1 are not used in im1
  */
  long int  k, *shft, n;
  PIX_TYPE *p, *plast, *p1, *p2;
  PIX_TYPE val;
  UCHAR *p3; /* image of seeds for flat regions */
  FIFO4 *q;
  int nx, ny, nz;
  int box[BOXELEM];

  n = objectpix(im2);
  if (n==ERROR)
    return ERROR;
  shft = (long int *)calloc(n, sizeof(long int));
  if (shft == NULL)
    return ERROR;

  /*  Take SE  into account  */
  box[0] = GetImNx(im2);
  box[1] = GetImNy(im2);
  box[2] = GetImNz(im2);
  box[3] = ox;
  box[4] = oy;
  box[5] = oz;
  set_shift_and_box((UCHAR *)GetImPtr(im2), box, GetImNx(im1), GetImNy(im1), shft);

  if (generic_framebox(im1,box,0)==ERROR){
    free((char*)shft);
    return ERROR;
  }

  nx = GetImNx(im1);
  ny = GetImNy(im1);
  nz = GetImNz(im1);


  p     = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p) /* shouldn't it be >= ???  for all (see below) */
      *p |= PIX_MSB;
  }

  q = create_fifo4((nx*ny*nz)/100L);
  if (q == NULL){
    free((char*)shft);
    return ERROR;
  }
  /* Here we go */
  p   = (PIX_TYPE *)GetImPtr(im1);
  p3  = (UCHAR *)GetImPtr(im3);
  for (; p < plast; p++, p3++){
    if (*p > PIX_MSB && *p3){  /* shouldn't it be >= ??? */
      val = *p;
      *p = PIX_MSB-1;
      fifo4_add(q, (intptr_t)p);
      while ((p1 = (PIX_TYPE *)fifo4_remove(q))){
	for (k=0; k < n; k++){
          p2 = p1 + shft[k];
	  if (*p2 == val){
	    *p2 = PIX_MSB-1;
	    fifo4_add(q, (intptr_t)p2);
	  }
	}
      }
    }
  }
  p   = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p>=PIX_MSB) /* shouldn't it be >= ???  for all (see below) */
      *p = 0;
    else if (*p > 0)
      *p =1;
  }
  free((char*)shft);
  free_fifo4(q);
  return NO_ERROR;
}
#include "g_undef.h"
#endif /* #ifndef NO_generic_IMAGE */


#include "us_def.h"
ERROR_TYPE us_seededplat(IMAGE *im1, IMAGE *im2, IMAGE *im3, int ox, int oy, int oz)
{
  /* each flat region marked by a seed is set to 1
     we assume that PIX_MSB and PIX_MSB-1 are not used in im1
  */
  long int  k, *shft, n;
  PIX_TYPE *p, *plast, *p1, *p2;
  PIX_TYPE val;
  UCHAR *p3; /* image of seeds for flat regions */
  FIFO4 *q;
  int nx, ny, nz;
  int box[BOXELEM];

  n = objectpix(im2);
  if (n==ERROR)
    return ERROR;
  shft = (long int *)calloc(n, sizeof(long int));
  if (shft == NULL)
    return ERROR;

  /*  Take SE  into account  */
  box[0] = GetImNx(im2);
  box[1] = GetImNy(im2);
  box[2] = GetImNz(im2);
  box[3] = ox;
  box[4] = oy;
  box[5] = oz;
  set_shift_and_box((UCHAR *)GetImPtr(im2), box, GetImNx(im1), GetImNy(im1), shft);

  if (us_framebox(im1,box,0)==ERROR){
    free((char*)shft);
    return ERROR;
  }

  nx = GetImNx(im1);
  ny = GetImNy(im1);
  nz = GetImNz(im1);


  p     = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p) /* shouldn't it be >= ???  for all (see below) */
      *p |= PIX_MSB;
  }

  q = create_fifo4((nx*ny*nz)/100L);
  if (q == NULL){
    free((char*)shft);
    return ERROR;
  }
  /* Here we go */
  p   = (PIX_TYPE *)GetImPtr(im1);
  p3  = (UCHAR *)GetImPtr(im3);
  for (; p < plast; p++, p3++){
    if (*p > PIX_MSB && *p3){  /* shouldn't it be >= ??? */
      val = *p;
      *p = PIX_MSB-1;
      fifo4_add(q, (intptr_t)p);
      while ((p1 = (PIX_TYPE *)fifo4_remove(q))){
	for (k=0; k < n; k++){
          p2 = p1 + shft[k];
	  if (*p2 == val){
	    *p2 = PIX_MSB-1;
	    fifo4_add(q, (intptr_t)p2);
	  }
	}
      }
    }
  }
  p   = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p>=PIX_MSB) /* shouldn't it be >= ???  for all (see below) */
      *p = 0;
    else if (*p > 0)
      *p =1;
  }
  free((char*)shft);
  free_fifo4(q);
  return NO_ERROR;
}
#include "us_undef.h"



#include "u32_def.h"
ERROR_TYPE u32_seededplat(IMAGE *im1, IMAGE *im2, IMAGE *im3, int ox, int oy, int oz)
{
  /* each flat region marked by a seed is set to 1
     we assume that PIX_MSB and PIX_MSB-1 are not used in im1
  */
  long int  k, *shft, n;
  PIX_TYPE *p, *plast, *p1, *p2;
  PIX_TYPE val;
  UCHAR *p3; /* image of seeds for flat regions */
  FIFO4 *q;
  int nx, ny, nz;
  int box[BOXELEM];

  n = objectpix(im2);
  if (n==ERROR)
    return ERROR;
  shft = (long int *)calloc(n, sizeof(long int));
  if (shft == NULL)
    return ERROR;

  /*  Take SE  into account  */
  box[0] = GetImNx(im2);
  box[1] = GetImNy(im2);
  box[2] = GetImNz(im2);
  box[3] = ox;
  box[4] = oy;
  box[5] = oz;
  set_shift_and_box((UCHAR *)GetImPtr(im2), box, GetImNx(im1), GetImNy(im1), shft);

  if (i32_framebox(im1,box,0)==ERROR){
    free((char*)shft);
    return ERROR;
  }

  nx = GetImNx(im1);
  ny = GetImNy(im1);
  nz = GetImNz(im1);


  p     = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p) /* shouldn't it be >= ???  for all (see below) */
      *p |= PIX_MSB;
  }

  q = create_fifo4((nx*ny*nz)/100L);
  if (q == NULL){
    free((char*)shft);
    return ERROR;
  }
  /* Here we go */
  p   = (PIX_TYPE *)GetImPtr(im1);
  p3  = (UCHAR *)GetImPtr(im3);
  for (; p < plast; p++, p3++){
    if (*p > PIX_MSB && *p3){  /* shouldn't it be >= ??? */
      val = *p;
      *p = PIX_MSB-1;
      fifo4_add(q, (intptr_t)p);
      while ((p1 = (PIX_TYPE *)fifo4_remove(q))){
	for (k=0; k < n; k++){
          p2 = p1 + shft[k];
	  if (*p2 == val){
	    *p2 = PIX_MSB-1;
	    fifo4_add(q, (intptr_t)p2);
	  }
	}
      }
    }
  }
  p   = (PIX_TYPE *)GetImPtr(im1);
  plast = p + nx * ny * nz;
  for (; p < plast; p++){
    if (*p>=PIX_MSB) /* shouldn't it be >= ???  for all (see below) */
      *p = 0;
    else if (*p > 0)
      *p =1;
  }
  free((char*)shft);
  free_fifo4(q);
  return NO_ERROR;
}
#include "u32_undef.h"

ERROR_TYPE seededplat(IMAGE *im1, IMAGE *im2, IMAGE *im3, int ox, int oy, int oz)
{

  switch (GetImDataType(im1)){

#ifndef NO_generic_IMAGE
  case t_GENERIC:
    return(generic_seededplat(im1,im2,im3,ox,oy,oz));
    break;
#endif

#ifndef NO_uc_IMAGE
  case t_UCHAR:
    return(uc_seededplat(im1,im2,im3,ox,oy,oz));
    break;
#endif

  case t_USHORT:
    return(us_seededplat(im1,im2,im3,ox,oy,oz));
    break;

  case t_UINT32:
    return(u32_seededplat(im1,im2,im3,ox,oy,oz));
    break;

  default:
    (void)sprintf(buf,"seededplat(): invalid pixel type\n"); errputstr(buf);
    return(ERROR);
  }
  return(NO_ERROR);
}

#include "uc_def.h"
ERROR_TYPE uc_labelpix(IMAGE *im)
{
  unsigned long int i;
  PIX_TYPE *p=(PIX_TYPE *)GetImPtr(im), lbl=1;

  for(i=GetImNx(im)*GetImNy(im)*GetImNz(im); i>0; i--,p++)
    if (*p)
      *p=lbl++;
  return NO_ERROR;
}
#include "uc_undef.h"


#include "us_def.h"
ERROR_TYPE us_labelpix(IMAGE *im)
{
  unsigned long int i;
  PIX_TYPE *p=(PIX_TYPE *)GetImPtr(im), lbl=1;

  for(i=GetImNx(im)*GetImNy(im)*GetImNz(im); i>0; i--,p++)
    if (*p)
      *p=lbl++;
  return NO_ERROR;
}
#include "us_undef.h"

#include "i32_def.h"
ERROR_TYPE i32_labelpix(IMAGE *im)
{
  unsigned long int i;
  PIX_TYPE *p=(PIX_TYPE *)GetImPtr(im), lbl=1;

  for(i=GetImNx(im)*GetImNy(im)*GetImNz(im); i>0; i--,p++)
    if (*p)
      *p=lbl++;
  return NO_ERROR;
}
#include "i32_undef.h"


ERROR_TYPE labelpix(IMAGE *im)
{

  switch (GetImDataType(im)){


  case t_UCHAR:
    return(uc_labelpix(im));
    break;

  case t_USHORT:
    return(us_labelpix(im));
    break;

  case t_INT32:
  case t_UINT32:
    return(i32_labelpix(im));
    break;


  default:
    (void)sprintf(buf,"labelpix(): invalid pixel type\n"); errputstr(buf);
    return(ERROR);
  }
  return(NO_ERROR);
}
/**@}*/
