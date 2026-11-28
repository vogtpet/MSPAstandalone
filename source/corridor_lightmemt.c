/***********************************************************************
Author(s): Pierre Soille and Peter Vogt
Copyright (C) 2008-2026 European Union (Joint Research Centre)

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

/** @file
 *  Morphological Segmentation of Binary Patterns \cite soille-vogt2009
 *  https://doi.org/10.1016/j.patrec.2008.10.015
 *  @author Pierre Soille and Peter Vogt
 *  Version 2.4, October 2026
 *
 *  This file contains the MSPA driver. It is the file referred to as fsp.c
 *  in the MSPA/GTB/GWB documentation and in the build instructions; the
 *  legacy file name corridor_lightmemt.c is kept for compatibility.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "miallib.h"
#include "op.h"

#ifdef _WIN32
#include <process.h>
#define mspa_getpid() ((long)_getpid())
#else
#include <unistd.h>
#define mspa_getpid() ((long)getpid())
#endif

#ifndef MSPA_PATH_MAX
#define MSPA_PATH_MAX 4096
#endif

/* Internal images that are spooled through the file system when the
 * -disk mode is used (and the hole mask needed for -internal 1).
 * They are written next to the output image (so that -odir is honoured
 * and a read-only working directory is fine) and their names contain
 * the process id, so concurrent runs do not overwrite each other. */
enum {
  MSPA_TMP_ALLHOLES = 0,
  MSPA_TMP_DISK_CORE1,
  MSPA_TMP_DISK_OUT,
  MSPA_TMP_DISK_PATCH,
  MSPA_TMP_DISK_OPEN,
  MSPA_TMP_DISK_CORE,
  MSPA_TMP_DISK_RESID,
  MSPA_TMP_COUNT
};

static const char *mspa_tmp_tag[MSPA_TMP_COUNT] = {
  "allholes", "core1", "out", "patch", "opening", "core", "residues"
};

static char mspa_tmp_path[MSPA_TMP_COUNT][MSPA_PATH_MAX];

static void mspa_tmp_cleanup(void)
{
  int i;

  for(i = 0; i < MSPA_TMP_COUNT; i++)
    if(mspa_tmp_path[i][0] != '\0')
      (void)remove(mspa_tmp_path[i]);
}

static void mspa_tmp_init(const char *outpath)
{
  char dir[MSPA_PATH_MAX] = ".";
  const char *s;
#ifdef _WIN32
  const char *s2;
#endif
  int i;

  if(outpath != NULL){
    s = strrchr(outpath, '/');
#ifdef _WIN32
    s2 = strrchr(outpath, '\\');
    if((s2 != NULL) && ((s == NULL) || (s2 > s)))
      s = s2;
#endif
    if(s != NULL){
      size_t len = (size_t)(s - outpath);
      if(len == 0){            /* output in the root directory */
	dir[0] = outpath[0];
	dir[1] = '\0';
      }
      else if(len < sizeof(dir)){
	memcpy(dir, outpath, len);
	dir[len] = '\0';
      }
    }
  }

  for(i = 0; i < MSPA_TMP_COUNT; i++)
    snprintf(mspa_tmp_path[i], sizeof(mspa_tmp_path[i]),
	     "%s/mspa_%ld_%s.tif", dir, mspa_getpid(), mspa_tmp_tag[i]);
}

/* Read one of the temporary images; abort with a clear message if the
 * file cannot be read (e.g. disk full or permission problem). */
static IMAGE *mspa_read_tmp(int slot)
{
  IMAGE *im;

  im = (IMAGE *)read_image(mspa_tmp_path[slot]);
  if(im == NULL){
    fprintf(stderr,"\nmspa: error: cannot read temporary image '%s'.\n",
	    mspa_tmp_path[slot]);
    fprintf(stderr,"mspa: check the free space and the write permissions "
	    "of the output directory.\n");
    mspa_tmp_cleanup();
    exit(ERROR);
  }

  return im;
}

extern ERROR_TYPE binOIthin_FIFO(IMAGE *imin, int stype, int atype, IMAGE *imanchor);
extern IMAGE *ced(IMAGE *ref, IMAGE *mask);
extern ERROR_TYPE dirmax(IMAGE *im, int dir);
extern ERROR_TYPE label(IMAGE *im1, IMAGE *im2, int ox, int oy, int oz);
extern ERROR_TYPE rdil(IMAGE *mark, IMAGE *mask, int graph, int flag);
extern ERROR_TYPE rero(IMAGE *mark, IMAGE *mask, int graph, int flag);
extern ERROR_TYPE set_regions(IMAGE *ilbl, IMAGE *ival, int indic);
extern ERROR_TYPE f_threshstrict(IMAGE *, float, float, float, float);
extern ERROR_TYPE d_threshstrict(IMAGE *, double, double, double, double);
extern ERROR_TYPE i32_setlevel(IMAGE *, INT32, INT32, INT32);
extern ERROR_TYPE i32_thresh(IMAGE *, INT32, INT32, INT32, INT32);
extern ERROR_TYPE wsfah(IMAGE *iml, IMAGE *imr, int graph, int maxfl);
extern ERROR_TYPE szgeocompat(IMAGE *im1, IMAGE *im2);
extern ERROR_TYPE arith(IMAGE *im1, IMAGE *im2, int op);
extern IMAGE *sqedt(IMAGE *, int);
extern IMAGE *read_image(char *);


#ifdef DEBUG_EDT
int edt_counter=0;
#endif

// Process has done i out of n rounds,
// and we want a bar of width w and resolution r.
static inline void loadBar(int x, int n, int r, int w)
{
    int step;

    // Only update r times.
    if ( (n <= 0) || (r <= 0) || (w <= 0) ) return;
    if ( r > n ) r = n;
    step = n / r;
    if ( step <= 0 ) return;
    if ( x % step != 0 ) return;

    // Calculate the ratio of complete-to-incomplete.
    float ratio = x/(float)n;
    int   c     = ratio * w;

    // Show the percentage complete.
    printf("%4d%% [", (int)(ratio*100) );

    // Show the load bar.
    for (x=0; x<c; x++)
       printf("=");
    for (x=c; x<w; x++)
       printf(" ");

    // ANSI Control codes to go back to the
    // previous line and clear it.
    //  printf("]\n\033[F\033[J");
    printf("]\r"); // Move to the first column
    fflush(stdout);
}


IMAGE *getexternalboundary(IMAGE *im, float size, float edu)
{
  IMAGE *nim, *edt;
  int sqedt_type;

  nim=(IMAGE *)copy_image(im);
  negation(nim);

  if ( (nim->nx > 65535) || (nim->ny > 65535) )
    sqedt_type = t_UINT64;
  else
   sqedt_type = t_UINT32;

  edt=uc_sqedt_band(nim, (float)(size*edu), 0, sqedt_type);
  if (edt != NULL)
    bitwise_op(edt, nim, AND_op);

  free_image(nim);

  return(edt);
}

IMAGE *fm_preproc(IMAGE *fm, float size, float edu)
{
  IMAGE *da, *fa, *dda, *dfa;

  da=(IMAGE *)copy_image(fm);
  generic_thresh(da, 1, 255, 0, 1); /* 0 missing 1 for background 2 for foreground */
  dda=getexternalboundary(da, size, edu);

#ifdef DEBUG
  writeTiffOneStripPerLine(dda, "dda.tif" , NULL);
#endif

  free_image(da);
  fa=(IMAGE *)copy_image(fm);
  generic_thresh(fa, 2, 255, 0, 1);
  dfa=getexternalboundary(fa, size, edu);

#ifdef DEBUG
  writeTiffOneStripPerLine(dfa, "dfa.tif" , NULL);
  printf("hello 1 in fm_preproc\n");
#endif

  arith(dfa, fa, OR_op);
  arith(dfa, dda, INF_op);
  arith(dfa, fa, SUP_op);

  free_image(fa);
  free_image(dda);

  return (dfa);
}

/*!
 * Add a frame of width equal to size and propagate the values of each
 * respective image border into the frame to mitigate border effects.
 * Propagate also the corner values in their respective enlarged
 * quadrants.
 */
IMAGE *fm_preproc2(IMAGE *im, int size)
{
  IMAGE *lb, *rb, *tb, *bb, *bbim, *lbim, *rbim, *tbim, *corner_im;
  int box[6];
  UCHAR *p;
  UCHAR ul_val, ur_val, ll_val, lr_val; /* corner values */
  
  mia_size_t nx = GetImNx(im);
  mia_size_t ny = GetImNy(im);

  /* get corner values */
  p = (UCHAR *)GetImPtr(im);
  ul_val = p[0];
  ur_val = p[nx-1];
  ll_val = p[nx*(ny-1)];
  lr_val = p[(nx*ny)-1];

/* 	 (lb (*imcut fm 0 0 0 0 (- (*getny fm) 1) 0)) */
  lb=imcut(im, 0, 0, 0, 0, GetImNy(im)-1, 0);
  
/* 	 (lbim (*imcreate t_UCHAR size (*getny fm) 1)) */
  lbim=create_image(t_UCHAR, size, GetImNy(im), 1);
  
/* 	 (rb (*imcut fm (- (*getnx fm) 1) */
/* 		     0 */
/* 		     0 */
/* 		     (- (*getnx fm) 1) */
/* 		     (- (*getny fm) 1) */
/* 		     0) */
/* 	     ) */
  rb=imcut(im, GetImNx(im)-1, 0, 0, GetImNx(im)-1, GetImNy(im)-1, 0);
  
/* 	 (rbim (*imcreate t_UCHAR size (*getny fm) 1)) */
  rbim=create_image(t_UCHAR, size, GetImNy(im), 1);
  
/* 	 (tb (*imcut fm */
/* 		     0 */
/* 		     0 */
/* 		     0 */
/* 		     (- (*getnx fm) 1) */
/* 		     0 */
/* 		     0) */
/* 	     ) */
  tb=imcut(im, 0, 0, 0, GetImNx(im)-1, 0, 0);
/* 	 (tbim (*imcreate t_UCHAR (*getnx fm) size 1)) */
  tbim=create_image(t_UCHAR, GetImNx(im), size, 1);
/* 	 (bb (*imcut fm */
/* 		     0 */
/* 		     (- (*getny fm) 1) */
/* 		     0 */
/* 		     (- (*getnx fm) 1) */
/* 		     (- (*getny fm) 1) */
/* 		     0) */
/* 	     ) */
  bb=imcut(im, 0, GetImNy(im)-1, 0, GetImNx(im)-1, GetImNy(im)-1, 0);
/* 	 (bbim (*imcreate t_UCHAR (*getnx fm) size 1)) */
  bbim=create_image(t_UCHAR, GetImNx(im), size, 1);
  
/* 	 (out (*addframebox fm size size size size 0 0 0)) */
  box[0]=box[1]=box[2]=box[3]=size;
  box[4]=box[5]=0;
  generic_addframebox(im, box, 0);

/*     (@imputop out (@dirmax (@imputintop lbim lb */
/* 					(- (*getnx lbim) 1) */
/* 					0 */
/* 					0 */
/* 					OR_op) */
/* 			   3) */
/* 	      0 size 0 */
/* 	      OR_op) */
  imputop(lbim, lb, GetImNx(lbim)-1, 0, 0, OR_op);
  dirmax(lbim, 3);
  imputop(im, lbim, 0, size, 0, 11);

/*     (@imputop out (@dirmax (@imputintop rbim rb */
/* 					0 */
/* 					0 */
/* 					0 */
/* 					OR_op) */
/* 			   1) */
/* 	      (- (*getnx out) size 1) */
/* 	      size */
/* 	      0 */
/* 	      OR_op) */
  imputop(rbim, rb, 0, 0, 0, 11);
  dirmax(rbim, 1);
  imputop(im, rbim, GetImNx(im)-size, size, 0, OR_op);

/*     (@imputop out (@dirmax (@imputintop tbim tb */
/* 					0 */
/* 					(- (*getny tbim) 1) */
/* 					0 */
/* 					OR_op) */
/* 			   0) */
/* 	      size */
/* 	      0 */
/* 	      0 */
/* 	      OR_op) */
  imputop(tbim, tb, 0, GetImNy(tbim)-1, 0, OR_op);
  dirmax(tbim, 0);
  imputop(im, tbim, size, 0, 0, 11);

/*     (@imputop out (@dirmax (@imputintop bbim bb */
/* 					0 */
/* 					0 */
/* 					0 */
/* 					OR_op) */
/* 			   2) */
/* 	      size */
/* 	      (- (*getny out) size 1) */

/* 	      0 */
/* 	      OR_op) */
  imputop(bbim, bb, 0, 0, 0, 11);
  dirmax(bbim, 2);
  imputop(im, bbim, size, GetImNy(im)-size, 0, OR_op);

  free_image(rb); free_image(lb); free_image(bb); free_image(tb);
  free_image(rbim); free_image(lbim); free_image(tbim); free_image(bbim);
  
  /* create quadrant image, set it to each corner value, and insert it
     the respective quadrant */
  corner_im=create_image(t_UCHAR, size, size, 1);
  
  /* upper left quadrant */
  generic_blank(corner_im, ul_val);
  imputop(im, corner_im, 0, 0, 0, OR_op);

  /* upper right quadrant */
  generic_blank(corner_im, ur_val);
  imputop(im, corner_im, nx+size, 0, 0, OR_op);

  /* lower left quadrant */
  generic_blank(corner_im, ll_val);
  imputop(im, corner_im, 0, ny+size, 0, OR_op);

  /* lower right quadrant */
  generic_blank(corner_im, lr_val);
  imputop(im, corner_im, nx+size, ny+size, 0, OR_op);

  free_image(corner_im);

#ifdef DEBUG
  writeTiffOneStripPerLine(im, "im.tif", NULL);
#endif

  return im;
}

IMAGE *getcore(IMAGE *im, float size, float edu)
{
  int sqedt_type;

  if ( (im->nx > 65535) || (im->ny > 65535) )
    sqedt_type = t_UINT64;
  else
   sqedt_type = t_UINT32;

  return uc_sqedt_band(im, (float)(size*edu), 1, sqedt_type);
}

IMAGE *getpatch(IMAGE *im, float size, int graphfg, float edu, IMAGE *core_in)
{
  IMAGE *core;

/*   (*sub im (@rdil (*getcore im size) */
/* 		  im */
/* 		  graphfg) */
/* 	) */
/*   ) */
  core = (core_in != NULL) ? copy_image(core_in) : getcore(im, size, edu);
  if (core == NULL)
    return NULL;
  rdil(core, im, graphfg, 1);
  arith(core, im, SUBSWAP_op);

#ifdef DEBUG
  printf("hello 1 in getpatch\n");
  writeTiffOneStripPerLine(core, "rdil.tif", NULL);
#endif

  return(core);
}

IMAGE *uc_fillhole(IMAGE *im, int graph)
{
  IMAGE *marker;
  int box[BOXELEM];
  BOX_2D;

  marker=create_image(t_UCHAR, GetImNx(im), GetImNy(im), 1);
  generic_blank(marker,255);
  generic_framebox(marker,box,0);
  arith(marker, im, SUP_op);
  rero(marker, im, graph, 1);

  return marker;
}

IMAGE **setedges(IMAGE *im, float size, int graphfg, int graphbg, float edu,
                int disk, IMAGE *core_in, IMAGE *outer_in)
{
  IMAGE **iml;
  IMAGE *core, *outer, *corefill, *loecher, *edges;
  /* core1 is set either from a copy of core or, in -disk mode, from the
   * temporary file written below. The initialiser documents that both paths
   * assign it before use and silences GCC's -Wmaybe-uninitialized false
   * positive on older compilers (e.g. GCC 10 in Debian 11). */
  IMAGE *core1 = NULL;
  IMAGE *crt_edges;
  IMAGE *i0;
#ifdef DEBUG
  char aname[256];
#endif
#if DEBUG
  int counter=0;
#endif

  core = (core_in != NULL) ? copy_image(core_in) : getcore(im, size, edu);
  if (core == NULL)
    return NULL;

  if (disk==1)
    writeTiffOneStripPerLine(core, mspa_tmp_path[MSPA_TMP_DISK_CORE1], NULL);
  else
    core1=copy_image(core);

  corefill=uc_fillhole(core, graphbg);
  loecher=copy_image(corefill);
  arith(loecher, core, SUB_op);
  
#ifdef DEBUG
  printf("hello 1 in setedges\n");
  writeTiffOneStripPerLine(loecher, "loecher.tif", NULL);
#endif
  
  edges=create_image(t_UCHAR, GetImNx(im), GetImNy(im), 1);

  iml=calloc(2,sizeof(IMAGE *));

  int idx=22;
  loadBar(idx, 100, 100, 50);

  for(;;){
#ifdef DEBUG
    printf("counter = %d\n", counter);
    sprintf(aname, "corefill_counter%d.tif", counter);
    writeTiffOneStripPerLine(corefill, aname, NULL);
#endif
    
    volume(corefill);
    if (GetImVol(corefill)==0.0)
      break;
    crt_edges=getexternalboundary(corefill, size, edu);

#ifdef DEBUG
    sprintf(aname, "edges_before_bitwise_counter%d.tif", counter);
    writeTiffOneStripPerLine(edges, aname, NULL);
    sprintf(aname, "crt_edges_before_bitwise_counter%d.tif", counter);
    writeTiffOneStripPerLine(crt_edges, aname, NULL);
#endif
    
    bitwise_op(edges, crt_edges, OR_op);
    
#ifdef DEBUG
    sprintf(aname, "edges_after_bitwise_counter%d.tif", counter);
    writeTiffOneStripPerLine(edges, aname, NULL);
    sprintf(aname, "crt_edges_after_bitwise_counter%d.tif", counter);
    writeTiffOneStripPerLine(crt_edges, aname, NULL);
#endif
    
    free_image(crt_edges);
    i0=uc_fillhole(loecher, graphfg);
    
#ifdef DEBUG
    sprintf(aname, "i0_counter%d.tif", counter);
    writeTiffOneStripPerLine(i0, aname, NULL);
    printf("hello 2 in setedges\n");
#endif
    
    free_image(loecher);
    arith(corefill, i0, SUB_op);
    
    free_image(i0);
    arith(core, corefill, SUB_op);
    
#ifdef DEBUG
    printf("hello 3 in setedges\n");
#endif
    
    free_image(corefill);
    corefill=uc_fillhole(core, graphbg);
    loecher=copy_image(corefill);
    arith(loecher, core, SUB_op);
    
#ifdef DEBUG
    printf("hello 4 in setedges\n");
    counter++;
#endif
  }
  iml[0]=edges;

  if(disk==1)
    core1=mspa_read_tmp(MSPA_TMP_DISK_CORE1);
  outer = (outer_in != NULL) ? copy_image(outer_in)
                             : getexternalboundary(core1, size, edu);
  if (outer == NULL){
    free_image(core);
    free_image(corefill);
    free_image(loecher);
    free_image(core1);
    free(iml);
    return NULL;
  }

  negation(core1);
  bitwise_op(edges, core1, AND_op);
  free_image(core1);

  
#ifdef DEBUG
  printf("hello 5 in setedges\n");
  writeTiffOneStripPerLine(outer, "test_outer.tif", NULL);
  writeTiffOneStripPerLine(edges, "test_edges.tif", NULL);
#endif
  
  arith(outer, edges, SUB_op);
  iml[1]=outer;

  free_image(core);
  free_image(corefill);
  free_image(loecher);

  return iml;
}

IMAGE *getexternalboundarygeodesic(IMAGE *im, IMAGE *mask, float size, float edu)
{
  //(defun *getexternalboundarygeodesic (im mask size)
  //  (@touchar
  //   (@thresh
  //    (*ced im mask)
  //    0.0001
  //    (+ (* size edu))
  //    0.0 1.0)
  //   )
  //  )
  IMAGE *imdst;

  imdst=(IMAGE *)ced(im, mask);

#ifdef DEBUG
  printf("hello in getexternalboundarygeodesic\n");
  writeTiffOneStripPerLine(imdst, "imdst_ced.tif", NULL);
#endif

  f_thresh(imdst, 0.0001, size*edu, 0.0, 1.0);
  to_uchar(imdst);
  
  return imdst;
}


IMAGE *getconnector2core(IMAGE *core, IMAGE *opening, IMAGE *residues, \
			 float size, int oitype, int graphfg, float edu, int disk)
{
  //    (let* (
  // 	 (sk (@sub (@binanchorskeloi (*or opening residues) core oitype)
  // 		   core)
  // 	     )
  //         ; (connector (*getexternalboundary sk (- size 1))) ; should be geodesic
  // 	 (connector (*getexternalboundarygeodesic sk
  // 						  (@or (*sub opening core) residues)
  // 						  (- size 1))) ; should be geodesic
  // 	 )
  //     (@and (@or connector sk) (@or (*sub opening core) residues) )
  // 					; we need to intersect with reconstruction
  //    (@and connector (*rdil sk (@or (*sub opening core) residues) graphfg))
  //
  //    )
  IMAGE *sk, *connector, *i0;

  if (disk==1)
    sk=mspa_read_tmp(MSPA_TMP_DISK_OPEN);
  else
    sk=copy_image(opening);

  if (disk==1)
    residues=mspa_read_tmp(MSPA_TMP_DISK_RESID);
  bitwise_op(sk, residues, OR_op);

  loadBar(48, 100, 100, 50);

  if (disk==1)
    core=mspa_read_tmp(MSPA_TMP_DISK_CORE);
  binOIthin_FIFO(sk, oitype, 1, core);

  loadBar(80, 100, 100, 50);

#ifdef DEBUG
  writeTiffOneStripPerLine(sk, "sk.tif", NULL);
  printf("hello 1 in getconnector2core\n");
#endif

  arith(sk, core, SUB_op);

  if (disk==1)
    i0=mspa_read_tmp(MSPA_TMP_DISK_OPEN);
  else
    i0=copy_image(opening);

  arith(i0, core, SUB_op);

#ifdef DEBUG
  printf("hello 2 in getconnector2core\n");
#endif

  bitwise_op(i0, residues, OR_op);
  if (disk==1){
    free_image(residues);
    free_image(core);
  }

  loadBar(84, 100, 100, 50);\
  
  connector=getexternalboundarygeodesic(sk, i0, size-1, edu);
  
  loadBar(88, 100, 100, 50);
  
  bitwise_op(connector, sk, OR_op);
  bitwise_op(connector, i0, AND_op);
  rdil(sk, i0, graphfg, 1);
  bitwise_op(connector, sk, AND_op);

  free_image(sk);
  free_image(i0);
  
  return connector;
}

/* (defun *getcorridor (connector core opening size oitype) */
/*   (let* ( */
/*          (cor (@setregions (@labelgraph (*tolong connector) graphfg)  */
/* 			   (*wsfah  */
/* 			    (*labelgraph (*tolong core) graphfg)  */
/* 			    (@setlevel (*or opening connector) 0 0 255)  */
/* 			    graphfg 254)  */
/* 			   20)); 20 for range */
/* 	) */
/*     (@setlevel cor 2147483647 2147483647 0) */
/*     (@thresh cor 1 2147483647 0 1) */
/*     (@touchar cor) */
/*     ) */
/*   ) */
IMAGE *getcorridor(IMAGE *connector, IMAGE *core, IMAGE *opening, \
		   float size, int oitype, int graph)
{
  IMAGE *lbl, *imref, *cor;
  IMAGE *se;
  unsigned char *pse;

  se=create_image(t_UCHAR, 3, 3, 1);
  pse=(unsigned char *)GetImPtr(se);
  pse[1]=1; pse[3]=1; pse[5]=1; pse[7]=1;
  if (graph==8){
    pse[0]=1; pse[2]=1; pse[6]=1; pse[8]=1;
  }
  lbl=to_int32(core);
  lbl->DataType = t_UINT32;
  label(lbl, se, 1, 1, 0);

  imref=copy_image(opening);
  bitwise_op(imref, connector, OR_op);
  generic_setlevel(imref, 0, 0, 255);

  lbl->DataType=t_UINT32;
  wsfah(lbl, imref, graph, 254);
  free_image(imref);

  cor=to_int32(connector);
  cor->DataType = t_UINT32;
  label(cor, se, 1, 1, 0);
  cor->DataType = t_INT32;

  lbl->DataType=t_INT32;
  set_regions(cor, lbl, 20); // 20 for range

  free_image(lbl);
  
  i32_setlevel(cor,  2147483647, 2147483647, 0);
  i32_thresh(cor, 1, 2147483647, 0, 1);
  to_uchar(cor);
  
  free_image(se);
  
  return cor;
}

int fsp(char fnin[], char fnout[], float size, int graphfg, float edu, int disk, int transition, int internal)
{
  IMAGE *im, *i0, *core, *patch;
  IMAGE *allHoles, *coreHoles;
  IMAGE *opening, *edges, *perforation, *residues;
  IMAGE *outer;
  IMAGE *connector, *shortcut, *corridor;
  IMAGE **iml;
  IMAGE *tmp, *tmp2;
  IMAGE *out;
  int index;
  int box[6];
  int graphbg, oitype;
  char desc[512];
  int bufsize;
  int status = NO_ERROR;

  /* Temporary files are created next to the output image */
  mspa_tmp_init(fnout);

  snprintf(desc, sizeof(desc),
	  "\nBased on Morphological Segmentation of Binary Patterns\n"
	  "by Pierre Soille and Peter Vogt\n"
	  "URL: http://dx.doi.org/10.1016/j.patrec.2008.10.015\n"
	  "See also: https://doi.org/10.5194/isprs-archives-XLVIII-4-W1-2022-427-2022\n" 
	  "File generated by mspa v2.4\n"
	  "Parameters: eew=%f, graphfg=%d, internal=%d, disk=%d", size, graphfg, internal, disk);

  size=(size+0.98)/sqrt(2);

  if (graphfg==8){
    graphbg=4;
    oitype=0;
  }
  else{
    graphbg=8;
    oitype=1;
  }
  if ( (size==1) && (transition==2) )
    transition=0;

  im=(IMAGE *)read_image(fnin);
  if(im == NULL){
    fprintf(stderr,"mspa: error: cannot read input image '%s'.\n", fnin);
    return ERROR;
  }
  loadBar(1, 100, 100, 50);
  // (setq bufsize (truncate (+ 0.5 (* size 1.5))))
  // (@fm_frame_in im bufsize)

  bufsize=(int)((size*1.5)+0.5);
  box[0]=box[1]=box[2]=box[3]=(int)(bufsize+1.5);
  box[4]=box[5]=0;
  generic_addframebox(im, box, 0);

  // get all holes
  if (internal==1){
    i0=fm_preproc(im, 1, edu); 
    allHoles=uc_fillhole(i0, graphbg);
    arith(allHoles, i0, SUB_op);
    writeTiffOneStripPerLine(allHoles, mspa_tmp_path[MSPA_TMP_ALLHOLES], NULL);
    free_image(allHoles);
  }
    
  // (setq i0 (*fm_preproc im size))
  i0=fm_preproc(im, size, edu);
  free_image(im);
  loadBar(6, 100, 100, 50);

#ifdef DEBUG
  printf("hello 1 in fsp\n");
  writeTiffOneStripPerLine(i0, "i0-after-preproc.tif" , NULL);
#endif

  // (@fm_frame_out i0 bufsize)
  subframebox(i0, box);

  //  (setq i0 (*fm_preproc2 i0 (+ 1 bufsize)))

#ifdef DEBUG
  printf("hello 2 in fsp\n");
  writeTiffOneStripPerLine(i0, "i0.tif", NULL);
#endif

  fm_preproc2(i0, bufsize+1);
  out=copy_image(i0);
  loadBar(8, 100, 100, 50);

#ifdef DEBUG
  writeTiffOneStripPerLine(i0, "i0-preproc2.tif", NULL);
#endif

  box[0]=box[1]=box[2]=box[3]=1;
  box[4]=box[5]=0;
  box[0]=box[1]=box[2]=box[3]=(int)(bufsize+1.5);
  box[4]=box[5]=0;

  // (setq core  (*getcore i0 size))
  core=getcore(i0, size, edu);

#ifdef DEBUG
  printf("hello 3 in fsp\n");
  writeTiffOneStripPerLine(core, "core.tif", NULL);
#endif

  //(setq patch (*getpatch i0 size graphfg))
  patch=getpatch(i0, size, graphfg, edu, core);

#ifdef DEBUG
  printf("hello 4 in fsp\n");
  writeTiffOneStripPerLine(patch, "patch.tif", NULL);
#endif

  loadBar(16, 100, 100, 50);
  // (setq outer (*getexternalboundary core size))
  // (setq opening (*add outer core))
  outer=getexternalboundary(core, size, edu);
  opening=copy_image(outer);
  arith(opening, core, ADD_op);


#ifdef DEBUG
  printf("hello 5 in fsp\n");
  writeTiffOneStripPerLine(opening, "opening.tif", NULL);
#endif

  // (setq iml (*setedges i0 size))
  // (setq edges (car iml))
  // (setq perforation (cadr iml))

  if (disk==1){
    writeTiffOneStripPerLine(out, mspa_tmp_path[MSPA_TMP_DISK_OUT], NULL);
    free_image(out);
    writeTiffOneStripPerLine(patch, mspa_tmp_path[MSPA_TMP_DISK_PATCH], NULL);
    free_image(patch);
    writeTiffOneStripPerLine(opening, mspa_tmp_path[MSPA_TMP_DISK_OPEN], NULL);
    free_image(opening);
    writeTiffOneStripPerLine(core, mspa_tmp_path[MSPA_TMP_DISK_CORE], NULL);
    free_image(core);
  }

  loadBar(20, 100, 100, 50);
  /* The disk path frees core above; let setedges recompute it there. */
  iml=setedges(i0, size, graphfg, graphbg, edu, disk,
               (disk==1) ? NULL : core, outer);
  free_image(outer);
  loadBar(42, 100, 100, 50);

  edges=iml[0];
  perforation=iml[1];

#ifdef DEBUG
  printf("hello 6 in fsp\n");
  writeTiffOneStripPerLine(iml[0], "edges.tif", NULL);
  writeTiffOneStripPerLine(iml[1], "perforation.tif", NULL);
#endif

  //  (setq residues (*sub i0 core patch perforation edges))
  residues=i0;

  if (disk==1){
    out=mspa_read_tmp(MSPA_TMP_DISK_OUT);
    patch=mspa_read_tmp(MSPA_TMP_DISK_PATCH);
    opening=mspa_read_tmp(MSPA_TMP_DISK_OPEN);
    core=mspa_read_tmp(MSPA_TMP_DISK_CORE);
#ifndef DEBUG
    remove(mspa_tmp_path[MSPA_TMP_DISK_OUT]);
    remove(mspa_tmp_path[MSPA_TMP_DISK_PATCH]);
    remove(mspa_tmp_path[MSPA_TMP_DISK_CORE1]);
#endif
  }

#ifdef DEBUG
  printf("hello 7 in fsp\n");
  writeTiffOneStripPerLine(residues, "test_residues.tif", NULL);
#endif
  
  arith(residues, core, SUB_op);

  loadBar(44, 100, 100, 50);
  
  arith(residues, patch, SUB_op);
  shift(patch, -3);
  bitwise_op(out, patch, OR_op);
  free_image(patch);

#ifdef DEBUG
  printf("hello 8 in fsp\n");
#endif

  arith(residues, perforation, SUB_op);
  shift(perforation, -2);
  bitwise_op(out, perforation, OR_op);
  free_image(perforation);

#ifdef DEBUG
  printf("hello 9 in fsp\n");
#endif

  arith(residues, edges, SUB_op);
  shift(edges, -1);
  bitwise_op(out, edges, OR_op);
  free_image(edges);

#ifdef DEBUG
  printf("hello 10 in fsp\n");
  writeTiffOneStripPerLine(residues, "residues.tif", NULL);
#endif

  loadBar(46, 100, 100, 50);

  //  (if (= transition 2)
  //	(progn
  //	  (setq connector (*getconnector2opening opening residues size oitype))
  //	  (setq corridor (*getcorridor2 connector opening size oitype))
  // 	  )
  //       (progn
  //  	(setq connector (*getconnector2core core opening residues size oitype))
  //	(setq corridor (*getcorridor connector core opening size oitype))
  //	)
  //  )

  if (disk==1){
    writeTiffOneStripPerLine(out, mspa_tmp_path[MSPA_TMP_DISK_OUT], NULL);
    free_image(out);
    // writeTiffOneStripPerLine(core, mspa_tmp_path[MSPA_TMP_DISK_CORE], NULL);
    free_image(core);
    // writeTiffOneStripPerLine(opening, mspa_tmp_path[MSPA_TMP_DISK_OPEN], NULL);
    free_image(opening);
    writeTiffOneStripPerLine(residues, mspa_tmp_path[MSPA_TMP_DISK_RESID], NULL);
    free_image(residues);
  }

  connector=getconnector2core(core, opening, residues, size, oitype, graphfg, edu, disk);
  loadBar(90, 100, 100, 50);

  if( (size!=1) && (disk!=1) )
    free_image(residues);

  if (disk==1){
    out=mspa_read_tmp(MSPA_TMP_DISK_OUT);
    core=mspa_read_tmp(MSPA_TMP_DISK_CORE);
    opening=mspa_read_tmp(MSPA_TMP_DISK_OPEN);
    if (size==1)
      residues=mspa_read_tmp(MSPA_TMP_DISK_RESID);
#ifndef DEBUG
    remove(mspa_tmp_path[MSPA_TMP_DISK_OUT]);
    remove(mspa_tmp_path[MSPA_TMP_DISK_CORE]);
    remove(mspa_tmp_path[MSPA_TMP_DISK_OPEN]);
    remove(mspa_tmp_path[MSPA_TMP_DISK_RESID]);
#endif
  }

#ifdef DEBUG
  printf("hello 11 in fsp\n");
 writeTiffOneStripPerLine(connector, "connector.tif", NULL);
#endif
 
  corridor=getcorridor(connector, core, opening, size, oitype, graphfg);
  free_image(opening);
  shift(core, -4);
  bitwise_op(out, core, OR_op);
  if(internal!=1)
    free_image(core);
  
#ifdef DEBUG
  printf("hello 12 in fsp\n");
  writeTiffOneStripPerLine(corridor, "corridor1.tif", NULL);
#endif

  //  (setq shortcut (*sub connector corridor))
  arith(connector, corridor, SUB_op);
  shortcut=connector;
#ifdef DEBUG
  printf("hello 13 in fsp\n");
#endif

  if (size == 1){
    tmp=copy_image(corridor);
    dilate4(tmp, 1, 1);
    arith(tmp, residues, INF_op);
    bitwise_op(corridor, tmp, OR_op);
    free_image(tmp);

  //  (@and corridor (@setlevel (@prune (@setlevel (*or opening corridor)  1 1 2) 2 4)
  //  			      2 2 1)
  //  	  )

    tmp=copy_image(shortcut);
    dilate4(tmp, 1, 1);
    arith(tmp, residues, INF_op);
    free_image(residues);
    bitwise_op(shortcut, tmp, OR_op);
    
#ifdef DEBUG
    printf("hello 14 in fsp\n");
    writeTiffOneStripPerLine(shortcut, "shortcut1.tif", NULL);
#endif

    free_image(tmp);
  }
  loadBar(93, 100, 100, 50);

  //  (@and shortcut (@setlevel (@prune (@setlevel (*or opening shortcut)  1 1 2) 2 4)
  //			      2 2 1)
  //	  )
  //  ;; end of patch
  
  // generate output

  shift(corridor, -5);
  shift(shortcut, -6);

  bitwise_op(out, corridor, OR_op);
  free_image(corridor);
  bitwise_op(out, shortcut, OR_op);
  free_image(shortcut);

  if (internal==1){
    shift(core, 4);
    tmp=uc_fillhole(core, graphbg);

    //if (disk==1){
      allHoles=mspa_read_tmp(MSPA_TMP_ALLHOLES);
#ifndef DEBUG
      remove(mspa_tmp_path[MSPA_TMP_ALLHOLES]);
#endif
    //}
    coreHoles=copy_image(tmp);
    arith(coreHoles, core, SUB_op);
    negation(coreHoles);
    bitwise_op(allHoles, coreHoles, AND_op);  /* A \ B=A \int (\complement B) */
    free_image(coreHoles);
    generic_setlevel(allHoles, 1, 1, 220);
    arith(tmp, core, SUB_op);
    free_image(core);

 #ifdef DEBUG
    printf("hello 15 in fsp\n");
 #endif

    tmp2=uc_fillhole(tmp, graphbg);
    generic_setlevel(tmp2, 1, 1, 100);
    arith(out, tmp2, ADD_op);
    arith(out, allHoles, SUP_op);
    
    free_image(allHoles);
    free_image(tmp); free_image(tmp2);
  }

  loadBar(97, 100, 100, 50);

  create_lut(out);
  index=0;
  out->lut[index]=56540;
  out->lut[index+256]=56540; /* assuming 256 entries in ColorMap! */
  out->lut[index+512]=56540; /* assuming 256 entries in ColorMap! */

  index=100;
  out->lut[index]=35000;
  out->lut[index+256]=35000; /* assuming 256 entries in ColorMap! */
  out->lut[index+512]=35000; /* assuming 256 entries in ColorMap! */

  index=1;
  out->lut[index]=65535;
  out->lut[index+256]=35980; /* assuming 256 entries in ColorMap! */
  out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

  index=101;
  out->lut[index]=65535;
  out->lut[index+256]=35980; /* assuming 256 entries in ColorMap! */
  out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

  index=3;
  out->lut[index]=0;
  out->lut[index+256]=0; /* assuming 256 entries in ColorMap! */
  out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

  index=103;
  out->lut[index]=0;
  out->lut[index+256]=0; /* assuming 256 entries in ColorMap! */
  out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

  index=5;
  out->lut[index]=0;
  out->lut[index+256]=0; /* assuming 256 entries in ColorMap! */
  out->lut[index+512]=65535; /* assuming 256 entries in ColorMap! */

  index=105;
  out->lut[index]=0;
  out->lut[index+256]=0; /* assuming 256 entries in ColorMap! */
  out->lut[index+512]=65535; /* assuming 256 entries in ColorMap! */

  index=9;
  out->lut[index]=41120;
  out->lut[index+256]=15420; /* assuming 256 entries in ColorMap! */
  out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

  index=109;
  out->lut[index]=41120;
  out->lut[index+256]=15420; /* assuming 256 entries in ColorMap! */
  out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

  index=17;
  out->lut[index]=0;
  out->lut[index+256]=51400; /* assuming 256 entries in ColorMap! */
  out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

  index=117;
  out->lut[index]=0;
  out->lut[index+256]=51400; /* assuming 256 entries in ColorMap! */
  out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

  index=33;
  out->lut[index]=65535;
  out->lut[index+256]=0; /* assuming 256 entries in ColorMap! */
  out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

  index=133;
  out->lut[index]=65535;
  out->lut[index+256]=0; /* assuming 256 entries in ColorMap! */
  out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

  index=65;
  out->lut[index]=65535;
  out->lut[index+256]=65535; /* assuming 256 entries in ColorMap! */
  out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

  index=165;
  out->lut[index]=65535;
  out->lut[index+256]=65535; /* assuming 256 entries in ColorMap! */
  out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

  index=129;
  out->lut[index]=65535;
  out->lut[index+256]=65535; /* assuming 256 entries in ColorMap! */
  out->lut[index+512]=65535; /* assuming 256 entries in ColorMap! */

  index=220;
  out->lut[index]=50000;
  out->lut[index+256]=50000; /* assuming 256 entries in ColorMap! */
  out->lut[index+512]=50000; /* assuming 256 entries in ColorMap! */

  if (transition==1){
    index=35;
    out->lut[index]=65535;
    out->lut[index+256]=0; /* assuming 256 entries in ColorMap! */
    out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

    index=135;
    out->lut[index]=65535;
    out->lut[index+256]=0; /* assuming 256 entries in ColorMap! */
    out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

    index=67;
    out->lut[index]=65535;
    out->lut[index+256]=65535; /* assuming 256 entries in ColorMap! */
    out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

    index=167;
    out->lut[index]=65535;
    out->lut[index+256]=65535; /* assuming 256 entries in ColorMap! */
    out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

    index=37;
    out->lut[index]=65535;
    out->lut[index+256]=0; /* assuming 256 entries in ColorMap! */
    out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

    index=137;
    out->lut[index]=65535;
    out->lut[index+256]=0; /* assuming 256 entries in ColorMap! */
    out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

    index=69;
    out->lut[index]=65535;
    out->lut[index+256]=65535; /* assuming 256 entries in ColorMap! */
    out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

    index=169;
    out->lut[index]=65535;
    out->lut[index+256]=65535; /* assuming 256 entries in ColorMap! */
    out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */
  }
  else{
    index=35;
    out->lut[index]=0;
    out->lut[index+256]=0; /* assuming 256 entries in ColorMap! */
    out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

    index=135;
    out->lut[index]=0;
    out->lut[index+256]=0; /* assuming 256 entries in ColorMap! */
    out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

    index=67;
    out->lut[index]=0;
    out->lut[index+256]=0; /* assuming 256 entries in ColorMap! */
    out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

    index=167;
    out->lut[index]=0;
    out->lut[index+256]=0; /* assuming 256 entries in ColorMap! */
    out->lut[index+512]=0; /* assuming 256 entries in ColorMap! */

    index=37;
    out->lut[index]=0;
    out->lut[index+256]=0; /* assuming 256 entries in ColorMap! */
    out->lut[index+512]=65535; /* assuming 256 entries in ColorMap! */

    index=137;
    out->lut[index]=0;
    out->lut[index+256]=0; /* assuming 256 entries in ColorMap! */
    out->lut[index+512]=65535; /* assuming 256 entries in ColorMap! */

    index=69;
    out->lut[index]=0;
    out->lut[index+256]=0; /* assuming 256 entries in ColorMap! */
    out->lut[index+512]=65535; /* assuming 256 entries in ColorMap! */

    index=169;
    out->lut[index]=0;
    out->lut[index+256]=0; /* assuming 256 entries in ColorMap! */
    out->lut[index+512]=65535; /* assuming 256 entries in ColorMap! */
  }
  loadBar(98, 100, 100, 50);

  //  (@fm_postproc control im)
  //  (@fm_frame_out control bufsize)
  box[0]=box[1]=box[2]=box[3]=(int)(bufsize+1);
  box[4]=box[5]=0;
  subframebox(out, box);

  im=(IMAGE *)read_image(fnin);
  if(im == NULL){
    fprintf(stderr,"mspa: error: cannot read input image '%s'.\n", fnin);
    mspa_tmp_cleanup();
    return ERROR;
  }
  generic_thresh(im, 1, 255, 129, 0);
  arith(out, im, MASK_op);
  free_image(im);

  if(writeTiffOneStripPerLine(out, fnout, desc) != NO_ERROR){
    fprintf(stderr,"mspa: error: cannot write output image '%s'.\n", fnout);
    status = ERROR;
  }
  loadBar(100, 100, 100, 50);

  printf(" \n");

#ifndef DEBUG
  mspa_tmp_cleanup();
#endif

  return status;
}

