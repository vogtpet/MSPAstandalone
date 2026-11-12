/** Morphological Image Analysis (MIA) group
 ** by Jacopo.Grazzini@jrc.it
 **
 ** File frag.h 
 **
 ** Last update: 31/07/07
 **/
#ifndef   	_FRAG_H_
#define   	_FRAG_H_

#include "mia_parse.h"   /* ParseArg is used below */

#ifndef _FUNC_PAR_STRUCT_
#define _FUNC_PAR_STRUCT_
typedef struct funcpar {
  char iname[MAXNAMELENGTH]; // input image name
  char oname[MAXNAMELENGTH]; 
  char odir[MAXNAMELENGTH];  // output directory
  /* note: MAXNAMELENGTH is defined in mia_parse.h */
  int pgraphfg;             // connectivity foreground
  double psize;                // size
  int flag_disk;          // flag (boolean)
  int transition;         
  double pedu;              // length of diagonal
  int internal;             //
} funcPar;
#endif


#ifndef ABSURD_PEDU
#define ABSURD_PEDU -1.
#endif
#ifndef DEF_PEDU
#define DEF_PEDU sqrt(2.)
#endif
#ifndef DEF_MAX_PEDU
#define DEF_MAX_PEDU 10. // change here if absurd
#endif


#ifndef ABSURD_PSIZE
#define ABSURD_PSIZE 0.0
#endif
#ifndef DEF_PSIZE
#define DEF_PSIZE 1.0
#endif
#ifndef DEF_MAX_PSIZE
#define DEF_MAX_PSIZE 255.0  // change here if absurd
#endif

#ifndef ABSURD_PGRAPHFG
#define ABSURD_PGRAPHFG -1
#endif
#ifndef DEF_PGRAPHFG
#define DEF_PGRAPHFG 8
#endif
#ifndef DEF_MAX_PGRAPHFG
#define DEF_MAX_PGRAPHFG 8 // change here if absurd
#endif
#ifndef DEF_MIN_PGRAPHFG
#define DEF_MIN_PGRAPHFG 4 // change here if absurd
#endif

#ifndef DEF_INTERNAL
#define DEF_INTERNAL 1
#endif
#ifndef ABSURD_INTERNAL
#define ABSURD_INTERNAL -1
#endif
#ifndef DEF_MAX_INTERNAL
#define DEF_MAX_INTERNAL 1 // change here if absurd
#endif
#ifndef DEF_MIN_INTERNAL
#define DEF_MIN_INTERNAL 0 // change here if absurd
#endif



#ifndef DEF_TRANSITION
#define DEF_TRANSITION 1
#endif
#ifndef ABSURD_TRANSITION
#define ABSURD_TRANSITION -1
#endif
#ifndef DEF_MAX_TRANSITION
#define DEF_MAX_TRANSITION 1 // change here if absurd
#endif
#ifndef DEF_MIN_TRANSITION
#define DEF_MIN_TRANSITION 0 // change here if absurd
#endif

#ifndef DEF_ONAME
#define DEF_ONAME "out.tif"
#endif
#ifndef DEF_ODIR
#define DEF_ODIR "./"
#endif

#ifdef __cplusplus
extern "C"
{
#endif	/* __cplusplus */
  
  int parse_define( int narg, ParseArg *arg, funcPar *fpar );
  int func_init( funcPar *fpar );
  int func_parse(int argc, char *argv[], funcPar *fpar);
  int func_check( funcPar *fpar );

#ifdef __cplusplus
}
#endif		/* __cplusplus */



#endif 	    /* !_FUNC_H_ */
