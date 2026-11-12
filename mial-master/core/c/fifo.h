/***********************************************************************
Author(s): Pierre Soille
Copyright (C) 2000-2020 European Union (Joint Research Centre)

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

#ifndef _FIFO_H_
#define _FIFO_H_

#include <stdint.h> /* REQUIRED FOR intptr_t POINTER ALIGNMENT */

/*
 ** FIFO type definition
 */

/* Replace everything from the FIFO4 struct definition downwards in fifo.h */

typedef struct {
  intptr_t *qp;
  intptr_t *qps;
  intptr_t *qpr;
  intptr_t *qplast;
  intptr_t *qpl;
  long int mod;
} FIFO4;

#define  FICT_PIX  1

/* Declare the actual underlying 64-bit functions */
/* Ensure this is at the bottom of fifo.h right before #endif */
extern FIFO4 *create_fifo4(long int);
extern void  mial_fifo4_add_64(FIFO4 *, intptr_t);
extern intptr_t mial_fifo4_remove_64(FIFO4 *);
extern intptr_t mial_fifo4_look_64(FIFO4 *);
extern void fifo4_lookreset(FIFO4 *);
extern long int fifo4_empty(FIFO4 *);
extern void  fifo4_increase(FIFO4 *);
extern void  free_fifo4(FIFO4 *);
extern void fifo4_flush(FIFO4 *);

#ifndef FIFO_C_
  /* Force parameters to evaluate safely inside a 64-bit integer typecast,
     completely wiping out any embedded (long int) truncation markers */
  #define fifo4_add(q, val)    mial_fifo4_add_64((q), (intptr_t)(val))
  #define fifo4_remove(q)      ((intptr_t)mial_fifo4_remove_64(q))
  #define fifo4_look(q)        ((intptr_t)mial_fifo4_look_64(q))
#endif



#endif
