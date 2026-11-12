/** Morphological Image Analysis (MIA) group
 ** by Jacopo.Grazzini@jrc.it
 **
 **
 ** File func.h
 **
 ** Last update: 31/07/07
 **/
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "mia_parse.h"

#include "frag.h"

extern int fsp(char fnin[], char fnout[], float size, int graphfg, float edu, int disk, int transition, int internal);

int FlagVerbose=FALSE;

/* Return TRUE if path is absolute on the current platform. */
static int mspa_path_is_absolute(const char *path)
{
  if((path == NULL) || (path[0] == '\0'))
    return FALSE;

#if defined(_WIN32) || defined(__CYGWIN__)
  /* UNC path (\\server\share), rooted path (\dir or /dir) or drive letter */
  if((path[0] == '\\') || (path[0] == '/'))
    return TRUE;
  if((((path[0] >= 'A') && (path[0] <= 'Z')) ||
      ((path[0] >= 'a') && (path[0] <= 'z'))) && (path[1] == ':'))
    return TRUE;
#endif

  return (path[0] == '/') ? TRUE : FALSE;
}

/* Join a directory and a file name into out (of size n).
 * Absolute file names are used as-is; a separator is only added when
 * the directory does not already end with one.
 * Returns OK or ERROR (buffer too small). */
static int mspa_path_join(char *out, size_t n, const char *dir, const char *name)
{
  const char *sep = "";
  size_t dlen;

  if((out == NULL) || (n == 0) || (name == NULL))
    return ERROR;

  if((dir == NULL) || (dir[0] == '\0') || mspa_path_is_absolute(name))
    return (snprintf(out, n, "%s", name) < (int)n) ? OK : ERROR;

  dlen = strlen(dir);
  if((dir[dlen-1] != '/') && (dir[dlen-1] != '\\'))
    sep = "/";

  return (snprintf(out, n, "%s%s%s", dir, sep, name) < (int)n) ? OK : ERROR;
}

int func_test( funcPar *fpar ){
  printf("\nParameters");
  printf("\niname=%s",fpar->iname);
  printf("\noname=%s",fpar->oname);
  printf("\nodir=%s",fpar->odir);
  printf("\nedu=%g",fpar->pedu);
  printf("\ngraphfg=%d",fpar->pgraphfg);
  printf("\nsize=%g",fpar->psize);
  printf("\nflag=%d\n",fpar->transition);

  return OK;
}

/******************************************************/
int main(int argc, char **argv) {
/******************************************************/
  funcPar *fpar; /* structure defining the parametes of the function
		  * if you don't like it, just declare the fields of
		  * the structure as variables of the main */
  char full_out_path[2 * MAXNAMELENGTH + 2];
  int status;

  if((fpar=(funcPar*)calloc(1,sizeof(funcPar))) == NULL) {
    fprintf(stderr,"\nOut of memory.\n");
    return ERROR;
  }
  func_init(fpar);

  if(func_parse(argc,argv,fpar) == ERROR ) {
    free(fpar);
    return ERROR;
  }

  func_check( fpar );

  if (FlagVerbose)
    func_test( fpar );

  /* Build the output path: absolute -o values are kept as they are,
   * otherwise -odir is prepended (with a single separator). */
  if(mspa_path_join(full_out_path, sizeof(full_out_path),
		    fpar->odir, fpar->oname) != OK) {
    fprintf(stderr,"\nError: output path is too long.\n");
    free(fpar);
    return ERROR;
  }

  status = fsp(fpar->iname, full_out_path, (float) fpar->psize,
	       fpar->pgraphfg,(float) fpar->pedu,fpar->flag_disk,
	       fpar->transition,fpar->internal);

  free(fpar);

  return status;
}


/******************************************************/
int func_parse(int argc, char *argv[], funcPar *fpar) {
/******************************************************/
  ParseArg* atom;
  int narg;

  if((atom=parse_alloc(NARGMAX)) == NULL) return ERROR;
  narg = parse_init(atom);

  narg = parse_define( narg, atom, fpar );
  narg = parse_load( narg, argc, argv, atom );

  parse_free( atom );

  return (narg == ERROR) ? ERROR : OK;
} // end of main


/******************************************************/
int parse_define( int narg, ParseArg *atom, funcPar *fpar ) {
/******************************************************/
  int in=narg;

  //   Argument Input. Type : String
  strcpy(atom[in].argname,"-i");
  strcpy(atom[in].valname,"input");
  sprintf(atom[in].explain," %s : Name of the input image.\n",atom[in].argname);
  atom[in].type=IS_STRING;
  atom[in].var.c=&(fpar->iname[0]);
  atom[in].bufsize=MAXNAMELENGTH;
  atom[in].num=1, in++;

  //   Argument Output. Type : String
  strcpy(atom[in].argname,"-o");
  strcpy(atom[in].valname,"output");
  sprintf(atom[in].explain," %s : Name of the output image; default: \"%s\". \n",
	  atom[in].argname, DEF_ONAME);
  atom[in].type=IS_STRING;
  atom[in].var.c=&(fpar->oname[0]);
  atom[in].bufsize=MAXNAMELENGTH;
  atom[in].num=1, in++;

   //   Argument PEDU. Type: Double
/*   strcpy(atom[in].argname,"-edu");  /\* name of the flag for defining this variable *\/ */
/*   strcpy(atom[in].valname,"length_diagonal"); /\* short description used for display *\/ */
/*   sprintf(atom[in].explain," %s : diagonal length; default: %g\n", */
/* 	  atom[in].argname,DEF_PEDU); /\* full description for yourself and also */
/* 				       * called with '-help' or '-usage' *\/ */
/*   atom[in].type=IS_DOUBLE; /\* type of the variable *\/ */
/*   atom[in].var.d=&(fpar->pedu); /\* address of the variable where the input value will */
/* 				  * be stored *\/ */
/*   atom[in].minval.d=0.,   atom[in].maxval.d=DEF_MAX_PEDU; /\* possible min and max values *\/ */
/*   atom[in].num=1; /\* number of input values to follow this flag *\/ */
/*   in++; /\* next flag *\/ */




  //   Argument PSIZE. Type: Double
  strcpy(atom[in].argname,"-eew");
  strcpy(atom[in].valname,"psize");
  sprintf(atom[in].explain," %s : Effective Edge Width; default: %f.\n",
	  atom[in].argname,DEF_PSIZE);
  atom[in].type=IS_DOUBLE;
  atom[in].var.d=&(fpar->psize);
  atom[in].minval.d=1.0,   atom[in].maxval.d=DEF_MAX_PSIZE;
  atom[in].num=1, in++; /* next flag */

  //   Argument PGRAPHFG. Type: Int
  strcpy(atom[in].argname,"-graphfg");
  strcpy(atom[in].valname,"pgraphfg");
  sprintf(atom[in].explain," %s : Foreground connectivity; default: %d.\n", atom[in].argname, DEF_PGRAPHFG);
  atom[in].type=IS_INT;
  atom[in].var.i=&(fpar->pgraphfg);
  atom[in].minval.i=DEF_MIN_PGRAPHFG,   atom[in].maxval.i=DEF_MAX_PGRAPHFG;
  atom[in].num=1, in++;

  //   Argument INTERNAL. Type: Int
  strcpy(atom[in].argname,"-internal");
  strcpy(atom[in].valname,"internal");
  sprintf(atom[in].explain," %s : Internal flag; default: %d.\n", atom[in].argname, DEF_INTERNAL);
  atom[in].type=IS_INT;
  atom[in].var.i=&(fpar->internal);
  atom[in].minval.i=DEF_MIN_INTERNAL,  atom[in].maxval.i=DEF_MAX_INTERNAL;
  atom[in].num=1, in++;


  //   Argument . Type :
  strcpy(atom[in].argname,"-odir");
  strcpy(atom[in].valname,"output_dir");
  sprintf(atom[in].explain," %s : Name of the output directory; default: \"%s\".\n",
	  atom[in].argname,DEF_ODIR);
  atom[in].type=IS_STRING;
  atom[in].var.c=&(fpar->odir[0]);
  atom[in].bufsize=MAXNAMELENGTH;
  atom[in].num=1, in++;



  //   Argument TRANS. Type: Int
  strcpy(atom[in].argname,"-transition");
  strcpy(atom[in].valname,"transition");
  sprintf(atom[in].explain," %s : Transition flag; default: %d.\n", atom[in].argname, DEF_INTERNAL);
  atom[in].type=IS_INT;
  atom[in].var.i=&(fpar->transition);
  atom[in].minval.i=DEF_MIN_TRANSITION,  atom[in].maxval.i=DEF_MAX_TRANSITION;
  atom[in].num=1, in++;


/*   //   Argument FLAG_PTRANS. Type: Flag */
/*   strcpy(atom[in].argname,"-trans");   */
/*   strcpy(atom[in].valname,"flag_ptransition");  */
/*   sprintf(atom[in].explain," %s : Boolean flag; default: TRUE\n",atom[in].argname);    */

/*   atom[in].type=IS_FLAG; */
/*   atom[in].flag=&(fpar->flag_ptrans); */
/*   atom[in].num=0, in++; */


  //   Argument FLAG_DISK: Type Flag
  strcpy(atom[in].argname,"-disk");
  strcpy(atom[in].valname,"flag_disk");
  sprintf(atom[in].explain," %s : Flag for minimum RAM usage\n",atom[in].argname);
  atom[in].type=IS_FLAG;
  atom[in].flag=&(fpar->flag_disk);
  atom[in].num=0, in++;


  return in; // returns the total number of parameters
} // end of func_define

/******************************************************/
int func_init( funcPar *fpar ) {
/******************************************************/
  /* calloc() normally zeroes the structure, but do not rely on it */
  fpar->iname[0] = '\0';
  fpar->oname[0] = '\0';
  fpar->odir[0]  = '\0';
  fpar->pgraphfg = ABSURD_PGRAPHFG;
  fpar->psize = ABSURD_PSIZE;
  fpar->pedu = ABSURD_PEDU;
  fpar->internal = TRUE;
  fpar->flag_disk = FALSE;
  fpar->transition = TRUE;
  return OK;
} // end of func_init


/******************************************************/
int func_check( funcPar *fpar ) {
/******************************************************/

  if(!strlen(fpar->iname)) {
    fprintf(stderr,"\nNo input given (option '-i'). Exiting...\n");
    exit(ERROR);
  }

  if(!strlen(fpar->oname)) {
    if (FlagVerbose)
      printf("\nNo output given (option '-o'). Default name: %s", DEF_ONAME);
    strcpy(fpar->oname,DEF_ONAME);
  }

  if(!strlen(fpar->odir)) {
    if (FlagVerbose)
      printf("\nNo output given (option '-odir'). Default directory: %s", DEF_ODIR);
    strcpy(fpar->odir,DEF_ODIR);
  }

  if(fpar->pgraphfg <= ABSURD_PGRAPHFG) {
    if (FlagVerbose)
      printf("\nSetting variable pgraphfg to default value: %d",DEF_PGRAPHFG);
    fpar->pgraphfg=DEF_PGRAPHFG;
  }
  else if( (fpar->pgraphfg != 4) && (fpar->pgraphfg != 8) ) {
    fprintf(stderr,"\noption '-graphfg' must be either 4 or 8. Exiting...\n");
    exit(ERROR);
  }

  if(fpar->internal <= ABSURD_INTERNAL) {
    if (FlagVerbose)
      printf("\nSetting variable internal to default value: %d",DEF_INTERNAL);
    fpar->internal=DEF_INTERNAL;
  }
  else if( (fpar->internal != 0) && (fpar->internal != 1) ) {
    fprintf(stderr,"\noption '-internal' must be either 0 or 1 (default). Exiting...\n");
    exit(ERROR);
  }

  if(fpar->transition <= ABSURD_TRANSITION) {
    if (FlagVerbose)
      printf("\nSetting variable internal to default value: %d",DEF_TRANSITION);
    fpar->transition=DEF_TRANSITION;
  }
  else if( (fpar->transition != 0) && (fpar->transition != 1) ) {
    fprintf(stderr,"\noption '-transition ' must be either 0 or 1 (default). Exiting...\n");
    exit(ERROR);
  }

  if(fpar->psize <= ABSURD_PSIZE) {
    fpar->psize=DEF_PSIZE;
    if (FlagVerbose)
      printf("\nSetting variable psize to default value: %f", fpar->psize);
  }

  if(fpar->pedu <= ABSURD_PEDU) {
    if (FlagVerbose)
      printf("\nSetting variable pedu to default value: %f",DEF_PEDU);
    fpar->pedu=DEF_PEDU;
  }

  return OK;
} // end of func_check


