# ------------------------------------------------------------
#
# note: compile with -DCURE_CLUSTER for additional types in set_regions but requires additional library
#
# Detect the system architecture
UNAME_M := $(shell uname -m)

BASE       = miallib
TARGET     = lib$(BASE)
CC         = gcc
CXX        = c++

# defined for debugging: -DDEBUG -DXLDEBUG -DXLDEBUG2 -DXLDEBUG -DDEBUB
# skipped: -DNNI
# to be done:  -Wextra -Wno-sign-compare

CFLAGS     = -DNOSHP -DCLASSIF -DODOITHIN -DMCISRG -std=c99 -O3 -Wall -c -Wno-unknown-pragmas -I${CONDA_PREFIX}/mial/include/

# 3. Handle Architecture-Specific Flag (-m64)
IS_ARM = no

ifneq ($(findstring arm,$(UNAME_M)),)
    IS_ARM = yes
endif
ifneq ($(findstring aarch64,$(UNAME_M)),)
    IS_ARM = yes
endif
# Fix: Detect MSYS2 CLANGARM64 environment correctly
ifneq ($(findstring ARM64,$(MSYSTEM)),)
    IS_ARM = yes
endif
ifneq ($(findstring CLANGARM64,$(MSYSTEM)),)
    IS_ARM = yes
endif

ifeq ($(IS_ARM),no)
    CFLAGS += -m64
endif

# 4. Handle Compiler-Specific OpenMP Flag
# Check if the compiler is Clang
IS_CLANG := $(shell $(CC) --version 2>/dev/null | grep -i clang)

ifneq ($(IS_CLANG),)
    # Fix: Native LLVM Clang (MSYS2 ARM64) uses standard -fopenmp for compiling,
    # but uses -lomp for the linking phase.
    CFLAGS += -DOPENMP -fopenmp
    LDFLAGS += -lomp
else
    # Linux/macOS/Windows GCC OpenMP syntax
    CFLAGS += -DOPENMP -fopenmp
    LDFLAGS += -lgomp
endif


CFLAGS-XLISP  = -DXLISP
CFLAGS-MSPA   = -DMSPA

prefix     = /usr/local
exec_prefix= ${prefix}

# NOTES:
# skipped so far: smoothcc_grazzja.c
# note that natngbint.c # requires NNI to be defined and the libcsa and libnn libraries.  libnn makefile,in needs to pe patched to include -fPIC when compiling
# note that phase_correlation.c requires -lfftw3
# note that gsl.c requires -lgls -lgslcblas
# skel.c and classification.c requires -DODOITHIN and -DCLASSIF respectively

# min_max return type master (G_TYPE *) different from type specific (ERROR_TYPE)

SRCS	   = imem.c shm.c pointop.c \
		geom.c bresenham.c \
		imio_gdal.c imio2.c imio.c \
		imstat.c classification.c histo.c  indexx.c \
		registration.c gsl.c \
		format.c colconv.c  miscel.c \
		setshft.c \
		fifo.c fah.c pqueue.c \
		dcluster.c \
		label.c mslabel.c mmlabel.c \
		setreg.c setreglut.c \
		labelccfastrim.c labelccms.c labelccmi.c labelci.c labelcims.c labelccvar.c \
		labelccdissim.c labelccmsdissim.c newlabelcc.c \
		labelvertex.c propagate.c \
		outeredge.c \
		alphacc.c alphatree.c alphatreetoCCs.c \
		dendro.c \
		partorp.c partition.c \
		erodil.c lerodil.c \
		herk.c herkbl.c \
		rank.c \
		convolve.c phase_correlation.c \
		dist.c efedt.c \
		geodist.c ced.c ggeo.c \
		recons.c rminmax.c \
		wshed.c wsfah.c \
		srg.c mcisrg.c \
		regionMean.c determineSize.c pqueueExact.c segmentation.c \
		simplifyLine.c linepool.c borderdetection.c vectorize.c douglas-peucker.c  \
		writeShapeFile.c writeSVG.c \
		flow.c aflood.c  fillocarve.c flatdir.c  htop.c complete.c \
		shade.c \
		skelodthin.c skel.c epc.c \
		switch.c \
		oiht.c oiiz.c oiws.c \
		uswilk.c hull.c hullti.c chull.c myhull.c \
		dirmean.c \
		grid.c projection.c \
		edgeweight.c transition.c \
		dbscan.c \
		mblincomb.c \
		ovlmatrix.c \
		msmm.c \
		natngbint.c \
		remsens.c  \
		mspa.c \
		compose.c

OBJS       = $(SRCS:.c=.o)

SRCS-MSPA = fifo.c label.c wshed.c bresenham.c pointop.c wsfah.c	\
		ced.c lerodil.c oiht.c imem.c erodil.c imio.c skel.c	\
		recons.c imstat.c pqueue.c fah.c efedt.c miscel.c	\
		format.c setreg.c setshft.c geom.c

OBJS-MSPA  = $(SRCS-MSPA:.c=.o)

BUILDDIR   = ../build
LDIR 	   = $(BUILDDIR)/lib
DOCDIR     = ../doc


SDIR-XLISP	   =  $(BUILDDIR)/build-static-xlisp
SOBJS-XLISP	   =  $(SRCS:%.c=$(SDIR-XLISP)/%.o)

SDIR-GENERIC	   =  $(BUILDDIR)/build-static-generic
SDIR-MSPA	   =  $(BUILDDIR)/build-static-mspa
SOBJS-GENERIC	   =  $(SRCS:%.c=$(SDIR-GENERIC)/%.o)
SOBJS-MSPA         =  $(SRCS-MSPA:%.c=$(SDIR-MSPA)/%.o)

DDIR-GENERIC	   =  $(BUILDDIR)/build-shared-generic
DDIR-MSPA	   =  $(BUILDDIR)/build-shared-mspa
DOBJS-GENERIC	   =  $(SRCS:%.c=$(DDIR-GENERIC)/%.o)
DOBJS-MSPA	   =  $(SRCS-MSPA:%.c=$(DDIR-MSPA)/%.o)

INCLUDE    = -I./ -I/mnt/jeoproc/envs/LUCHAP/conda/mial/include/python${PYVER}/ -I/usr/include/gdal/

# TODO: add -lcsa -lnn

ifdef $(NOSHP)
	LIBS       = -lgdal -ltiff -ldl -lproj -lfftw3_omp -lgsl -lgslcblas
else
	LIBS       = -lgdal -ltiff -ldl -lproj -lfftw3_omp -lgsl -lgslcblas -lshp
endif

INTERFACE  = imem.i
SWIGOPT    =
SWIG       = swig
RUNTIMEDIR = $(exec_prefix)/lib

LIBM       = -lieee -lm
LIBC       =
LIBCRYPT   = -lcrypt
SYSLIBS    = $(LIBM) $(LIBC) $(LIBCRYPT)

libtool_comp = $(TOP)/../Tools/libtool --mode compile
libtool_link = $(TOP)/../Tools/libtool --mode link

# X11 options

XLIB       = -L/usr/X11R6/lib -lX11
XINCLUDE   = -I/usr/X11R6/include

# Symbols used for using shared libraries
STATIC=		.a
LDSTATIC=	ar rcv
CCSTATIC=       -fPIC

# FIX 1: Detect Windows OS to apply correct Shared Library Extension (.dll vs .so)
SO=		.so
LDSHARED=	gcc -shared
CCSHARED=	-fPIC
CXXSHARED=      gcc -shared

ifeq ($(OS),Windows_NT)
    SO= .dll
    # Windows requires explicit binary stripping/runtime mapping on shared libraries
    LDSHARED= $(CC) -shared -Wl,--export-all-symbols
    CXXSHARED= $(CXX) -shared -Wl,--export-all-symbols
endif


##################################################################
#####                     SUPERCLASS                        ######
##################################################################

# FIX 2: Escaped quotes inside target command strings for cleaner multi-platform echo execution
banner.h: $(SRCS)
	echo "#define MYBANNER \"File created using programs by Pierre Soille et al. Version as of $$(date) with git tag $$(git describe --tags --dirty 2>/dev/null || echo 'untagged')\"" > banner.h
	echo "#define MIA_BANNER \"MIALlib version as of $$(date)\"" >> banner.h


##################################################################
#####                       MSPA                         ######
##################################################################

$(SDIR-MSPA)/%.o: %.c miallib.h banner.h
	@mkdir -p $(@D)
	$(CC) -c $(CCSTATIC) $(CFLAGS) $(CFLAGS-MSPA) $(INCLUDE) -o $@ $<

$(DDIR-MSPA)/%.o: %.c miallib.h banner.h
	@mkdir -p $(@D)
	$(CC) -c $(CCSHARED) $(CFLAGS) $(CFLAGS-MSPA) $(INCLUDE) -o $@ $<

$(LDIR)/$(TARGET)_mspa.a: $(SOBJS-MSPA)
	@mkdir -p $(@D)
	$(LDSTATIC) $@ $(SOBJS-MSPA) && ranlib $@

$(LDIR)/$(TARGET)_mspa$(SO): $(DOBJS-MSPA)
	@mkdir -p $(@D)
	$(LDSHARED) $(DOBJS-MSPA) $(LIBS) -o $@

##################################################################
#####                       GENERIC                         ######
##################################################################

$(SDIR-GENERIC)/%.o: %.c miallib.h banner.h
	@mkdir -p $(@D)
	$(CC) -c $(CCSTATIC) $(CFLAGS) $(INCLUDE) -o $@ $<

$(DDIR-GENERIC)/%.o: %.c miallib.h banner.h
	@mkdir -p $(@D)
	$(CC) -c $(CCSHARED) $(CFLAGS) $(INCLUDE) -o $@ $<

$(LDIR)/$(TARGET)_generic.a: $(SOBJS-GENERIC)
	@mkdir -p $(@D)
	$(LDSTATIC) $@ $(SOBJS-GENERIC) && ranlib $@

$(LDIR)/$(TARGET)_generic$(SO): $(DOBJS-GENERIC)
	@mkdir -p $(@D)
	$(LDSHARED) $(DOBJS-GENERIC) $(LIBS) -o $@


##################################################################
#####                       XLISP                           ######
##################################################################

# ----------------------------------------------------------------
# Build static library
# ----------------------------------------------------------------

$(SDIR-XLISP)/%.o: %.c miallib.h banner.h
	@mkdir -p $(@D)
	$(CC) -c $(CCSTATIC) $(CFLAGS) $(CFLAGS-XLISP)  $(INCLUDE) -o $@ $<

$(LDIR)/$(TARGET)_xlisp.a: $(SOBJS-XLISP)
	@mkdir -p $(@D)
	$(LDSTATIC) $@ $(SOBJS-XLISP) && ranlib $@


##################################################################
#####                       DOC                             ######
##################################################################

$(BUILDDIR)/doc/xml/mial_doxy2swig.i: $(DOCDIR)/Doxyfile
	cd $(DOCDIR) && $(MAKE) clean && $(MAKE) doc


##################################################################
#####                       BUILD                           ######
##################################################################


build: generic lib4mspa lib4xlisp

# FIX 3: Replaced raw extension targets with the dynamic $(SO) variable wrapper
generic: $(LDIR)/$(TARGET)_generic.a  $(LDIR)/$(TARGET)_generic$(SO)

lib4mspa: $(LDIR)/$(TARGET)_mspa.a $(LDIR)/$(TARGET)_mspa$(SO)

lib4xlisp: $(LDIR)/$(TARGET)_xlisp.a

doc: $(BUILDDIR)/doc/xml/mial_doxy2swig.i

all: build doc

# FIX 4: Ensured file extension wildcard matches both .so and .dll binaries smoothly during migration
install:
	mkdir -p $(prefix)/lib
	cp -a $(LDIR)/$(TARGET)_*$(SO) $(prefix)/lib/
	mkdir -p $(prefix)/include/$(BASE)/
	cp -a mialtypes.h $(prefix)/include/$(BASE)/
	cp -a miallib_*.h $(prefix)/include/$(BASE)/
	cp -a op.h $(prefix)/include/$(BASE)/

uninstall:
	-rm -f $(prefix)/lib/*miallib*
	-rm -f $(prefix)/include/$(BASE)/mialtypes.h
	-rm -f $(prefix)/include/$(BASE)/miallib_*.h
	-rm -f $(prefix)/include/$(BASE)/op.h

clean:
	@if [ "x$(BUILDDIR)" != "x" ] && [ -d "$(BUILDDIR)" ]; then rm -rf "$(BUILDDIR)"; fi
	-rm -f banner.h *.o
