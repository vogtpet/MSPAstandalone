# SPDX-License-Identifier: GPL-3.0-or-later
# Derived from the miallib MSPA implementation,
# Copyright (C) 2008-2026 European Union (Joint Research Centre).
"""
Pure-Python engine for Morphological Spatial Pattern Analysis (MSPA).

This module is a native-Python re-implementation of the MSPA algorithm
(``segmentBinaryPatterns``) originally written in C by Pierre Soille and
Peter Vogt as part of the ``miallib`` library (GuidosToolbox). It reproduces
the GTB/miallib MSPA result without a compiled C extension and is the
alternative implementation in ``python/``.

Public entry point
------------------
``segment_binary_patterns(imin, edge_width, graphfg=8, transition=1, internal=1)``
    imin : 2D uint8, 0=nodata, 1=background, 2=foreground.
    Returns the MSPA class-coded uint8 image (core 17, edge 3, perforation 5,
    islet 9, branch 1, bridge 33, loop 65, their transition 35/37/67/69 and
    internal +100 twins, core-opening 100, border-opening 220, missing 129).

Fidelity
--------
Bit-for-bit identical to the miallib MSPA C code on the repository test image
for the default, 4-connectivity and edge-width-5 reference runs, and across
the wider parameter sweep described in ``python/README.md``.  The frame
handling in ``fm_preproc2`` reproduces the C code exactly, including the
corner quadrants: the earlier trial left these at zero, which changed a
handful of border pixels.

Algorithm map (miallib source -> function here)
-----------------------------------------------
  segmentBinaryPatterns (mspa.c) -> segment_binary_patterns
  getcore / getexternalboundary / getpatch / uc_fillhole / setedges (mspa.c)
  fm_preproc / fm_preproc2 + frame handling (mspa.c)
  binOIthin_FIFO anchored order-independent thinning (skel.c)
  ced constrained geodesic Euclidean distance (ced.c)
  getconnector2core / getcorridor (mspa.c)

Original MSPA algorithm (c) Pierre Soille and Peter Vogt (miallib, GPL v3);
see http://dx.doi.org/10.1016/j.patrec.2008.10.015
"""

from __future__ import annotations

import os
import sys
import time
from concurrent.futures import ThreadPoolExecutor

import numpy as np

from .cpu import available_cpus
from .workspace import active_workspace, alloc, release, workspace_context

try:
    from numba import njit, prange, get_num_threads, get_thread_id
    _HAVE_NUMBA = True
except Exception:  # pragma: no cover - numba is optional at runtime
    _HAVE_NUMBA = False

    def njit(*args, **kwargs):
        """No-op fallback so the module imports even without numba."""
        if len(args) == 1 and callable(args[0]) and not kwargs:
            return args[0]

        def _decorator(func):
            return func
        return _decorator

    prange = range

    def get_num_threads():
        return 1

    def get_thread_id():
        return 0


# --- simple_pixel 6x256 LUT (htab), transcribed verbatim from skel.c -------
# Row index = stype. For MSPA with graphfg=8 -> stype=0 (simple g=8, g'=4).
_HTAB = [
    # 0: simple g=8, g'=4
    [0,1,1,0,1,1,1,1,1,1,1,1,0,1,1,0,
     1,1,0,0,1,1,1,1,0,1,0,1,0,1,1,0,
     1,1,0,0,0,1,0,1,1,1,1,1,0,1,1,0,
     0,1,0,0,0,1,0,1,0,1,0,1,0,1,1,0,
     1,0,1,0,1,1,1,1,0,0,1,1,0,1,1,0,
     0,0,0,0,1,1,1,1,0,0,0,1,0,1,1,0,
     0,0,0,0,0,1,0,1,0,0,1,1,0,1,1,0,
     0,0,0,0,0,1,0,1,0,0,0,1,0,1,1,0,
     1,0,1,0,0,0,1,1,1,1,1,1,0,1,1,0,
     0,0,0,0,0,0,1,1,0,1,0,1,0,1,1,0,
     0,0,0,0,0,0,0,1,1,1,1,1,0,1,1,0,
     0,0,0,0,0,0,0,1,0,1,0,1,0,1,1,0,
     0,0,1,0,0,0,1,1,0,0,1,1,0,1,1,0,
     0,0,0,0,0,0,1,1,0,0,0,1,0,1,1,0,
     0,0,0,0,0,0,0,1,0,0,1,1,0,1,1,0,
     0,0,0,0,0,0,0,1,0,0,0,1,0,1,1,0],
    # 1: simple g=4, g'=8
    [0,1,1,0,1,0,0,0,1,0,0,0,0,0,0,0,
     0,1,1,0,1,1,0,0,1,0,0,0,0,0,0,0,
     0,1,1,0,1,0,0,0,1,1,0,0,0,0,0,0,
     0,1,1,0,1,1,0,0,1,1,0,0,0,1,0,0,
     0,1,1,0,1,0,1,0,1,0,0,0,0,0,0,0,
     0,1,1,0,1,1,1,1,1,0,0,0,0,0,0,0,
     0,1,1,0,1,0,1,0,1,1,0,0,0,0,0,0,
     0,1,1,0,1,1,1,1,1,1,0,0,0,1,0,1,
     0,1,1,0,1,0,0,0,1,0,1,0,0,0,0,0,
     0,1,1,0,1,1,0,0,1,0,1,0,0,0,0,0,
     0,1,1,0,1,0,0,0,1,1,1,1,0,0,0,0,
     0,1,1,0,1,1,0,0,1,1,1,1,0,1,0,1,
     0,1,1,0,1,0,1,0,1,0,1,0,0,0,1,0,
     0,1,1,0,1,1,1,1,1,0,1,0,0,0,1,1,
     0,1,1,0,1,0,1,0,1,1,1,1,0,0,1,1,
     0,1,1,0,1,1,1,1,1,1,1,1,0,1,1,0],
    # 2: simple g=4, g'=4 Ronse's {4,8}
    [0,1,1,0,1,0,0,0,1,0,0,0,0,0,0,0,
     0,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,
     0,1,0,0,0,0,0,0,1,1,0,0,0,0,0,0,
     0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,
     0,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,
     0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,
     0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
     0,0,0,0,0,1,0,1,0,0,0,0,0,1,0,0,
     0,0,1,0,0,0,0,0,1,0,1,0,0,0,0,0,
     0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
     0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
     0,0,0,0,0,0,0,0,0,1,0,1,0,1,0,0,
     0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,
     0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,0,
     0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,0,
     0,0,0,0,0,0,0,1,0,0,0,1,0,1,1,0],
    # 3: w-simple g=8, g'=4
    [1,1,1,0,1,1,1,1,1,1,1,1,0,1,1,0,
     1,1,0,0,1,1,1,1,0,1,0,1,0,1,1,0,
     1,1,0,0,0,1,0,1,1,1,1,1,0,1,1,0,
     0,1,0,0,0,1,0,1,0,1,0,1,0,1,1,0,
     1,0,1,0,1,1,1,1,0,0,1,1,0,1,1,0,
     0,0,0,0,1,1,1,1,0,0,0,1,0,1,1,0,
     0,0,0,0,0,1,0,1,0,0,1,1,0,1,1,0,
     0,0,0,0,0,1,0,1,0,0,0,1,0,1,1,0,
     1,0,1,0,0,0,1,1,1,1,1,1,0,1,1,0,
     0,0,0,0,0,0,1,1,0,1,0,1,0,1,1,0,
     0,0,0,0,0,0,0,1,1,1,1,1,0,1,1,0,
     0,0,0,0,0,0,0,1,0,1,0,1,0,1,1,0,
     0,0,1,0,0,0,1,1,0,0,1,1,0,1,1,0,
     0,0,0,0,0,0,1,1,0,0,0,1,0,1,1,0,
     0,0,0,0,0,0,0,1,0,0,1,1,0,1,1,0,
     0,0,0,0,0,0,0,1,0,0,0,1,0,1,1,0],
    # 4: w-simple g=4, g'=8
    [1,1,1,0,1,0,0,0,1,0,0,0,0,0,0,0,
     1,1,1,0,1,1,0,0,1,0,0,0,0,0,0,0,
     1,1,1,0,1,0,0,0,1,1,0,0,0,0,0,0,
     1,1,1,0,1,1,0,0,1,1,0,0,0,1,0,0,
     1,1,1,0,1,0,1,0,1,0,0,0,0,0,0,0,
     1,1,1,0,1,1,1,1,1,0,0,0,0,0,0,0,
     1,1,1,0,1,0,1,0,1,1,0,0,0,0,0,0,
     1,1,1,0,1,1,1,1,1,1,0,0,0,1,0,1,
     1,1,1,0,1,0,0,0,1,0,1,0,0,0,0,0,
     1,1,1,0,1,1,0,0,1,0,1,0,0,0,0,0,
     1,1,1,0,1,0,0,0,1,1,1,1,0,0,0,0,
     1,1,1,0,1,1,0,0,1,1,1,1,0,1,0,1,
     1,1,1,0,1,0,1,0,1,0,1,0,0,0,1,0,
     1,1,1,0,1,1,1,1,1,0,1,0,0,0,1,1,
     1,1,1,0,1,0,1,0,1,1,1,1,0,0,1,1,
     1,1,1,0,1,1,1,1,1,1,1,1,0,1,1,0],
    # 5: w-simple g=4, g'=4 Ronse's {4,8}
    [1,1,1,0,1,0,0,0,1,0,0,0,0,0,0,0,
     1,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,
     1,1,0,0,0,0,0,0,1,1,0,0,0,0,0,0,
     0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,
     1,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,
     0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,
     0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
     0,0,0,0,0,1,0,1,0,0,0,0,0,1,0,0,
     1,0,1,0,0,0,0,0,1,0,1,0,0,0,0,0,
     0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
     0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
     0,0,0,0,0,0,0,0,0,1,0,1,0,1,0,0,
     0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,
     0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,0,
     0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,0,
     0,0,0,0,0,0,0,1,0,0,0,1,0,1,1,0],
]

# simple_pair2[stype][code] (diagonal neighbour), skel.c:395
_SIMPLE_PAIR2 = [
    [0,1,1,1],   # simple g=8, g'=4
    [1,1,1,0],   # simple g=4, g'=8
    [0,1,1,0],   # simple g=4, g'=4 Ronse's {4,8}
    [1,1,1,1],   # w-simple g=8, g'=4
    [1,1,1,0],   # w-simple g=4, g'=8
    [1,1,1,0],   # w-simple g=4, g'=4
]

# simple_pair4[stype][code] (non-diagonal neighbour), skel.c:398
_SIMPLE_PAIR4 = [
    [0,1,1,1,1,0,0,0,1,0,0,0,1,0,0,0],   # simple g=8, g'=4
    [0,0,0,1,0,0,0,1,0,0,0,1,1,1,1,0],   # simple g=4, g'=8
    [0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0],   # simple g=4, g'=4 Ronse's {4,8}
    [1,1,1,1,1,0,0,0,1,0,0,0,1,0,0,0],   # w-simple g=8, g'=4
    [1,0,0,1,0,0,0,1,0,0,0,1,1,1,1,0],   # w-simple g=4, g'=8
    [1,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0],   # w-simple g=4, g'=4
]

# NumPy versions of the LUTs for the numba-compiled thinning path.
_HTAB_A = np.asarray(_HTAB, dtype=np.int8)
_SIMPLE_PAIR2_A = np.asarray(_SIMPLE_PAIR2, dtype=np.int8)
_SIMPLE_PAIR4_A = np.asarray(_SIMPLE_PAIR4, dtype=np.int8)


# ===========================================================================
# Numba-accelerated anchored thinning (binOIthin_FIFO)
#
# binOIthin_FIFO is by far the hottest MSPA stage (~65% of the runtime on
# realistic data). The whole skel.c helper chain is re-expressed below on flat
# int32 buffers with the LUTs passed as NumPy arrays, so numba can compile it
# to native code. The logic mirrors the pure-Python helpers verbatim; the only
# differences are mechanical (array LUTs, preallocated integer queues instead
# of Python lists). If numba is unavailable the @njit decorator degrades to a
# no-op and these run as ordinary (slower) Python, preserving behaviour.
# ===========================================================================

@njit(cache=True)
def _nb_simple_pixel(p, stype, pos, nx, htab):
    if p[pos] <= 0:
        return 0
    code = (1 if p[pos - 1] == 1 else 0)
    code |= (1 if p[pos + 1] == 1 else 0) << 1
    code |= (1 if p[pos - nx] == 1 else 0) << 2
    code |= (1 if p[pos + nx] == 1 else 0) << 3
    code |= (1 if p[pos - nx - 1] == 1 else 0) << 4
    code |= (1 if p[pos + nx - 1] == 1 else 0) << 5
    code |= (1 if p[pos - nx + 1] == 1 else 0) << 6
    code |= (1 if p[pos + nx + 1] == 1 else 0) << 7
    return htab[stype, code]


@njit(cache=True)
def _nb_test_anchor(pa, atype, pos):
    if atype == 0:
        return True
    return pa[pos] == 0


@njit(cache=True)
def _nb_simple_pair(p, stype, pos, ngbpos, sh, sp2, sp4):
    """Returns (result, conftype, a, b, c, d)."""
    if ngbpos < 4:
        if ngbpos == 0:
            a = sh[2]; b = sh[4]; c = sh[3]; d = sh[5]
        elif ngbpos == 1:
            a = sh[2]; b = sh[6]; c = sh[3]; d = sh[7]
        elif ngbpos == 2:
            a = sh[0]; b = sh[4]; c = sh[1]; d = sh[6]
        else:
            a = sh[0]; b = sh[5]; c = sh[1]; d = sh[7]
        code = (1 if p[pos + d] >= p[pos] else 0)
        code |= (1 if p[pos + c] >= p[pos] else 0) << 1
        code |= (1 if p[pos + b] >= p[pos] else 0) << 2
        code |= (1 if p[pos + a] >= p[pos] else 0) << 3
        conftype = code + 5
        return sp4[stype, code], conftype, a, b, c, d
    else:
        if ngbpos == 4:
            a = sh[0]; b = sh[2]
        elif ngbpos == 5:
            a = sh[3]; b = sh[0]
        elif ngbpos == 6:
            a = sh[2]; b = sh[1]
        else:
            a = sh[1]; b = sh[3]
        code = (1 if p[pos + b] >= p[pos] else 0)
        code |= (1 if p[pos + a] >= p[pos] else 0) << 1
        conftype = code + 1
        return sp2[stype, code], conftype, a, b, 0, 0


@njit(cache=True)
def _nb_num_sngb(p, pos, sh, ngbnum):
    c = 0
    for i in range(ngbnum):
        if p[pos + sh[i]] >= p[pos]:
            c += 1
    return c


@njit(cache=True)
def _nb_testsimple(p, pa, atype, stype, pos, ngbp, sh, nx, htab):
    if (_nb_test_anchor(pa, atype, pos + ngbp)
            and _nb_simple_pixel(p, stype, pos + ngbp, nx, htab) != 0
            and p[pos] == p[pos + ngbp]):
        return 1
    return 0


@njit(cache=True)
def _nb_indep_simple(p, pa, stype, atype, pos, sh, nx, htab, sp2, sp4):
    allindep = 1
    ngbnum = 0
    aa = 0
    bb = 0
    ct = 0
    conftype = 0
    i = 0
    while i < 8 and allindep == 1:
        if _nb_testsimple(p, pa, atype, stype, pos, sh[i], sh, nx, htab) != 0:
            ngbnum += 1
            res, conftype, a, b, c, d = _nb_simple_pair(p, stype, pos, i, sh, sp2, sp4)
            if res == 0:
                allindep = 0
                aa = a; bb = b; ct = conftype
        i += 1
    if allindep == 1:
        ct = conftype

    if (allindep == 0) and (stype == 1 or stype == 2 or stype == 4 or stype == 5):
        if ct == 4:
            if (_nb_testsimple(p, pa, atype, stype, pos, aa, sh, nx, htab) != 0
                    and _nb_testsimple(p, pa, atype, stype, pos, bb, sh, nx, htab) != 0):
                zerocount = 0
                for j in range(4):
                    if p[pos + sh[j]] < p[pos]:
                        zerocount += 1
                if zerocount == 2:
                    allindep = 1
                    if stype < 3:
                        founddiff = 0
                        j = 0
                        while j < 4 and founddiff == 0:
                            if _nb_testsimple(p, pa, atype, stype, pos, sh[j], sh, nx, htab) != 0:
                                if _nb_num_sngb(p, pos + sh[j], sh, 4) != 2:
                                    founddiff = 1
                            j += 1
                        if founddiff == 0:
                            allindep = 0
        return allindep

    if (allindep == 1) and (stype < 3):
        if (((ngbnum == 2) and (ct == 2 or ct == 3 or ct == 6 or ct == 7 or ct == 9 or ct == 13))
                or ((ngbnum == 3) and (ct == 4 or ct == 8 or ct == 17))):
            founddiff = 0
            j = 0
            while j < 8 and founddiff == 0:
                if _nb_testsimple(p, pa, atype, stype, pos, sh[j], sh, nx, htab) != 0:
                    if _nb_num_sngb(p, pos + sh[j], sh, 8) != ngbnum:
                        founddiff = 1
                j += 1
            if founddiff == 0:
                allindep = 0
    return allindep


@njit(cache=True)
def _grow(q, used):
    """Grow a grow-only queue, preserving its dtype and first ``used`` entries."""
    return np.concatenate((q, q))


@njit(cache=True)
def _nb_binOIthin(p, pa, atype, stype, sh, nx, start, end, htab, sp2, sp4,
                  qcurrent, qnext, q2):
    """Core order-independent thinning on a flat framed buffer p (modified in
    place). Grow-only integer queues replace the Python-list FIFOs."""
    npix = p.shape[0]
    psimple = np.zeros(npix, dtype=np.uint8)
    nc = 0

    # seed: all independent simple non-anchor points
    for i in range(start, end):
        if (_nb_simple_pixel(p, stype, i, nx, htab) != 0
                and _nb_test_anchor(pa, atype, i)
                and _nb_indep_simple(p, pa, stype, atype, i, sh, nx, htab, sp2, sp4) != 0):
            psimple[i] = 1
            if nc == qcurrent.size:
                qcurrent = _grow(qcurrent, nc)
            qcurrent[nc] = i
            nc += 1
        else:
            psimple[i] = 0

    deleted = 1
    while deleted:
        deleted = 0
        n2 = 0
        # delete the whole current independent set
        for a in range(nc):
            i = qcurrent[a]
            deleted = 1
            psimple[i] = 0
            p[i] = 0
            if n2 == q2.size:
                q2 = _grow(q2, n2)
            q2[n2] = i
            n2 += 1
        # re-examine neighbours of deleted pixels
        nn = 0
        for a in range(n2):
            i = q2[a]
            for j in range(8):
                k = i + sh[j]
                if (_nb_test_anchor(pa, atype, k)
                        and _nb_simple_pixel(p, stype, k, nx, htab) != 0
                        and psimple[k] == 0
                        and _nb_indep_simple(p, pa, stype, atype, k, sh, nx, htab, sp2, sp4) != 0):
                    psimple[k] = 1
                    if nn == qnext.size:
                        qnext = _grow(qnext, nn)
                    qnext[nn] = k
                    nn += 1
        # swap current<-next (no copy needed)
        qcurrent, qnext = qnext, qcurrent
        nc = nn


def _getngbshift8(nx: int):
    """Neighbour offsets, exactly as skel.c getngbshift8 (4-conn then 8-conn)."""
    return [
        -1,       # 0 W
        1,        # 1 E
        -nx,      # 2 N
        nx,       # 3 S
        -nx - 1,  # 4 NW
        nx - 1,   # 5 SW
        -nx + 1,  # 6 NE
        nx + 1,   # 7 SE
        0,        # 8 centre
    ]


def _simple_pixel(p, stype, pos, nx):
    """Port of simple_pixel (skel.c:69). p is a flat uint8 buffer."""
    if p[pos] <= 0:
        return 0
    code = (1 if p[pos - 1] == 1 else 0)
    code |= (1 if p[pos + 1] == 1 else 0) << 1
    code |= (1 if p[pos - nx] == 1 else 0) << 2
    code |= (1 if p[pos + nx] == 1 else 0) << 3
    code |= (1 if p[pos - nx - 1] == 1 else 0) << 4
    code |= (1 if p[pos + nx - 1] == 1 else 0) << 5
    code |= (1 if p[pos - nx + 1] == 1 else 0) << 6
    code |= (1 if p[pos + nx + 1] == 1 else 0) << 7
    return _HTAB[stype][code]


def _test_anchor(panchor, atype, pos):
    """Port of test_anchor (skel.c:190): anchor pixels are never simple."""
    return (atype == 0) or (panchor[pos] == 0)


def _simple_pair(p, stype, pos, ngbpos, sh):
    """
    Port of simple_pair (skel.c:369). Returns (result, conftype, a, b, c, d).
    """
    if ngbpos < 4:  # non-diagonal neighbour
        if ngbpos == 0:
            a, b, c, d = sh[2], sh[4], sh[3], sh[5]
        elif ngbpos == 1:
            a, b, c, d = sh[2], sh[6], sh[3], sh[7]
        elif ngbpos == 2:
            a, b, c, d = sh[0], sh[4], sh[1], sh[6]
        else:  # 3
            a, b, c, d = sh[0], sh[5], sh[1], sh[7]
        code = (1 if p[pos + d] >= p[pos] else 0)
        code |= (1 if p[pos + c] >= p[pos] else 0) << 1
        code |= (1 if p[pos + b] >= p[pos] else 0) << 2
        code |= (1 if p[pos + a] >= p[pos] else 0) << 3
        conftype = code + 5
        return _SIMPLE_PAIR4[stype][code], conftype, a, b, c, d
    else:  # diagonal neighbour
        if ngbpos == 4:
            a, b = sh[0], sh[2]
        elif ngbpos == 5:
            a, b = sh[3], sh[0]
        elif ngbpos == 6:
            a, b = sh[2], sh[1]
        else:  # 7
            a, b = sh[1], sh[3]
        code = (1 if p[pos + b] >= p[pos] else 0)
        code |= (1 if p[pos + a] >= p[pos] else 0) << 1
        conftype = code + 1
        return _SIMPLE_PAIR2[stype][code], conftype, a, b, 0, 0


def _num_sngb(p, pos, sh, ngbnum):
    """Port of num_sngb (skel.c:425)."""
    return sum(1 for i in range(ngbnum) if p[pos + sh[i]] >= p[pos])


def _testsimple(p, panchor, atype, stype, pos, ngbp, sh, nx):
    """Port of testsimple (skel.c:436)."""
    if (_test_anchor(panchor, atype, pos + ngbp)
            and _simple_pixel(p, stype, pos + ngbp, nx)
            and p[pos] == p[pos + ngbp]):
        return 1
    return 0


def _indep_simple(p, panchor, stype, atype, pos, sh, nx):
    """Port of indep_simple (skel.c:444)."""
    allindep = 1
    ngbnum = 0
    aa = bb = 0
    ct = 0
    conftype = 0
    i = 0
    while i < 8 and allindep:
        if _testsimple(p, panchor, atype, stype, pos, sh[i], sh, nx):
            ngbnum += 1
            res, conftype, a, b, c, d = _simple_pair(p, stype, pos, i, sh)
            if res == 0:
                allindep = 0
                aa, bb, ct = a, b, conftype
        i += 1
    if allindep:
        ct = conftype

    if (allindep == 0) and (stype in (1, 2, 4, 5)):
        if ct == 4:  # check edge configuration
            if (_testsimple(p, panchor, atype, stype, pos, aa, sh, nx)
                    and _testsimple(p, panchor, atype, stype, pos, bb, sh, nx)):
                zerocount = sum(1 for i in range(4) if p[pos + sh[i]] < p[pos])
                if zerocount == 2:
                    allindep = 1
                    if stype < 3:
                        founddiff = 0
                        i = 0
                        while i < 4 and founddiff == 0:
                            if _testsimple(p, panchor, atype, stype, pos, sh[i], sh, nx):
                                if _num_sngb(p, pos + sh[i], sh, 4) != 2:
                                    founddiff = 1
                            i += 1
                        if founddiff == 0:
                            allindep = 0
        return allindep

    if (allindep == 1) and (stype < 3):
        # triple / quadruple of isolated simple pixels check
        if (((ngbnum == 2) and (ct in (2, 3, 6, 7, 9, 13)))
                or ((ngbnum == 3) and (ct in (4, 8, 17)))):
            founddiff = 0
            i = 0
            while i < 8 and founddiff == 0:
                if _testsimple(p, panchor, atype, stype, pos, sh[i], sh, nx):
                    if _num_sngb(p, pos + sh[i], sh, 8) != ngbnum:
                        founddiff = 1
                i += 1
            if founddiff == 0:
                allindep = 0
    return allindep


def binOIthin_FIFO(img: np.ndarray, stype: int, anchor: np.ndarray | None):
    """
    Order-independent anchored thinning, port of binOIthin_FIFO (skel.c:587).

    Parameters
    ----------
    img : 2D uint8 (values 0/1)
        Image to thin in place (a copy is made).
    stype : int
        Simpleness type (MSPA uses 0 for 8-connected foreground).
    anchor : 2D uint8 or None
        Anchor mask; anchor pixels are never deleted (atype=1 if given).

    Returns
    -------
    2D uint8 thinned image (same shape as input).
    """
    atype = 1 if anchor is not None else 0

    ny, nx0 = img.shape
    # frame of 2 px like generic_framebox(...,box={2,2,2,2,0,0})
    F = 2
    nx = nx0 + 2 * F
    nyf = ny + 2 * F
    buf = np.zeros((nyf, nx), dtype=np.uint8)
    data = (img > 0).astype(np.uint8)
    # generic_framebox() does not enlarge the image: it overwrites the outer
    # two rows/columns with the frame value (0) in place.  Reproduce that
    # before thinning, because those border pixels influence their neighbours.
    if ny >= F and nx0 >= F:
        data[:F, :] = 0
        data[-F:, :] = 0
        data[:, :F] = 0
        data[:, -F:] = 0
    buf[F:F + ny, F:F + nx0] = data
    p = buf.ravel()

    if atype:
        abuf = np.zeros((nyf, nx), dtype=np.uint8)
        adata = (anchor > 0).astype(np.uint8)
        if ny >= F and nx0 >= F:
            adata[:F, :] = 0
            adata[-F:, :] = 0
            adata[:, :F] = 0
            adata[:, -F:] = 0
        abuf[F:F + ny, F:F + nx0] = adata
        pa = abuf.ravel()
    else:
        # numba needs a concrete array (not None); an all-zero anchor with
        # atype=0 is never consulted (_nb_test_anchor returns True for atype=0).
        pa = np.zeros(1, dtype=np.uint8)

    sh = np.asarray(_getngbshift8(nx), dtype=np.int32)
    npix = p.size
    start = 2 * nx + 2
    end = npix - 2 * nx - 2

    # Flat indices are int32 while they fit, int64 only for maps beyond 2^31
    # pixels; Numba specialises the kernel for the queue dtype.
    qdtype = np.int32 if npix <= 2 ** 31 else np.int64
    cap = 4096 if npix > 4096 else npix
    qcurrent = np.empty(cap, dtype=qdtype)
    qnext = np.empty(cap, dtype=qdtype)
    q2 = np.empty(cap, dtype=qdtype)

    # The hot inner algorithm (seeding + iterative independent-set deletion)
    # runs in the numba-compiled core; p is modified in place.
    _nb_binOIthin(p, pa, atype, stype, sh, nx, start, end,
                  _HTAB_A, _SIMPLE_PAIR2_A, _SIMPLE_PAIR4_A,
                  qcurrent, qnext, q2)

    out = buf.reshape(nyf, nx)[F:F + ny, F:F + nx0]
    return (out > 0).astype(np.uint8)


# ===========================================================================
# Base morphology helpers (getcore, getexternalboundary, getpatch, setedges)
# ===========================================================================
from scipy import ndimage as _ndi

EDU = np.float32(np.sqrt(2.0))  # the C code stores edu as float


def _struct(graph: int) -> np.ndarray:
    """3x3 connectivity element: graph==8 -> full, graph==4 -> plus."""
    if graph == 8:
        return np.ones((3, 3), dtype=bool)
    s = np.zeros((3, 3), dtype=bool)
    s[1, :] = True
    s[:, 1] = True
    return s


def _edt(mask: np.ndarray) -> np.ndarray:
    """
    Exact Euclidean distance transform of a binary mask: for each nonzero
    pixel, distance to the nearest zero pixel. Equivalent to
    sqrt(sqedt(mask)) in miallib (uc_sqedt is the error-free EDT).
    """
    return _ndi.distance_transform_edt(mask.astype(bool))


@njit(cache=True, inline="always")
def _nb_dt1d(f, n, out, v, z):
    """1-D squared distance transform (lower envelope of parabolas)."""
    k = 0
    v[0] = 0
    z[0] = -1.0e30
    z[1] = 1.0e30
    for q in range(1, n):
        s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2.0 * q - 2.0 * v[k])
        while s <= z[k]:
            k -= 1
            s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2.0 * q - 2.0 * v[k])
        k += 1
        v[k] = q
        z[k] = s
        z[k + 1] = 1.0e30
    k = 0
    for q in range(n):
        while z[k + 1] < q:
            k += 1
        d = q - v[k]
        out[q] = d * d + f[v[k]]


@njit(parallel=True, cache=True)
def _nb_sqedt(mask, nt):
    """Exact squared Euclidean distance to the nearest zero, in 2-D.

    Separable two-pass lower-envelope transform.  The work is split into
    ``nt`` column/row chunks (instead of one ``prange`` iteration per row),
    which keeps the function cacheable and avoids recompiling on every
    process start.  ``mask`` is uint8; the result is uint32 squared distance,
    matching the miallib sqedt type for images up to 65535 px per side.
    """
    ny, nx = mask.shape
    g = np.empty((ny, nx), dtype=np.uint32)
    maxdim = nx if nx > ny else ny

    for t in prange(nt):
        v = np.empty(maxdim, dtype=np.int32)
        z = np.empty(maxdim + 1, dtype=np.float64)
        f = np.empty(maxdim, dtype=np.float64)
        out = np.empty(maxdim, dtype=np.float64)
        xs = (nx * t) // nt
        xe = (nx * (t + 1)) // nt
        for x in range(xs, xe):
            for y in range(ny):
                f[y] = 0.0 if mask[y, x] == 0 else 1.0e20
            _nb_dt1d(f, ny, out, v, z)
            for y in range(ny):
                val = out[y]
                if val > 4294967294.0:
                    val = 4294967294.0
                g[y, x] = np.uint32(val + 0.5)

    for t in prange(nt):
        v = np.empty(maxdim, dtype=np.int32)
        z = np.empty(maxdim + 1, dtype=np.float64)
        f = np.empty(maxdim, dtype=np.float64)
        out = np.empty(maxdim, dtype=np.float64)
        ys = (ny * t) // nt
        ye = (ny * (t + 1)) // nt
        for y in range(ys, ye):
            for x in range(nx):
                f[x] = g[y, x]
            _nb_dt1d(f, nx, out, v, z)
            for x in range(nx):
                val = out[x]
                if val > 4294967294.0:
                    val = 4294967294.0
                g[y, x] = np.uint32(val + 0.5)
    return g


@njit(parallel=True, cache=True)
def _nb_edt_band64(mask, nt, threshold, strict):
    """Thresholded exact EDT for images wider than 65535 px.

    miallib switches to ``ucu64_sqedt`` above 65535 px per side and compares
    a double-precision distance.  This kernel reproduces its two passes but
    writes only the thresholded uint8 mask, avoiding a full float64/uint64
    distance image (which would need another 8 bytes per pixel).

    ``strict`` selects getcore's ``threshold < dist < MIALFLOAT_MAX`` test;
    otherwise getexternalboundary's inclusive ``dist <= threshold`` is used.
    """
    ny, nx = mask.shape
    bigval = np.uint32(2 ** 31 - 1)
    g = np.empty((ny, nx), dtype=np.uint32)

    # Pass 1: vertical distance to the nearest zero, exactly as ucu64_sqedt.
    for t in prange(nt):
        xs = (nx * t) // nt
        xe = (nx * (t + 1)) // nt
        for x in range(xs, xe):
            if mask[0, x] == 0:
                g[0, x] = 0
            else:
                g[0, x] = bigval
            for y in range(1, ny):
                if mask[y, x] == 0:
                    g[y, x] = 0
                else:
                    g[y, x] = g[y - 1, x] + np.uint32(1)
            for y in range(ny - 2, -1, -1):
                below = g[y + 1, x] + np.uint32(1)
                if below < g[y, x]:
                    g[y, x] = below

    # Pass 2: combine the vertical distances horizontally and threshold.
    out = np.zeros((ny, nx), dtype=np.uint8)
    for t in prange(nt):
        ys = (ny * t) // nt
        ye = (ny * (t + 1)) // nt
        f = np.empty(nx, dtype=np.float64)
        d = np.empty(nx, dtype=np.float64)
        v = np.empty(nx, dtype=np.int32)
        z = np.empty(nx + 1, dtype=np.float64)
        for y in range(ys, ye):
            for x in range(nx):
                gv = np.float64(g[y, x])
                f[x] = gv * gv
            _nb_dt1d(f, nx, d, v, z)
            if strict:
                for x in range(nx):
                    dist = np.sqrt(d[x])
                    if dist > threshold and dist < 3.4028235e38:
                        out[y, x] = 1
            else:
                for x in range(nx):
                    if np.sqrt(d[x]) <= threshold:
                        out[y, x] = 1
    return out


def _edt32(mask: np.ndarray) -> np.ndarray:
    """Distance transform with the C code's numeric types.

    For images up to 65535 px per side miallib uses uint32 squared distances:
    it converts them to float32, takes a float32 square root and compares with
    a float32 threshold.  Above that it uses uint64 distances, converts them
    to double and compares in double precision.  Reproduce both so threshold
    ties (e.g. eew=1.02 -> size*edu == 2) behave identically.
    """
    large = max(mask.shape) > 65535
    if _HAVE_NUMBA and not large:
        import numba
        sq = _nb_sqedt(np.ascontiguousarray(mask, dtype=np.uint8),
                       max(1, numba.get_num_threads()))
        dist = sq.astype(np.float32)
        np.sqrt(dist, out=dist)
        return dist
    dist = _edt(mask)
    if large:
        return dist.astype(np.float64)
    sq = np.rint(dist * dist).astype(np.float32)
    return np.sqrt(sq).astype(np.float32)


def _threshold_edt(mask: np.ndarray, threshold: np.float32,
                   strict: bool) -> np.ndarray:
    """Return the thresholded distance mask with the C code's semantics.

    Uses the fused parallel kernel for maps wider/taller than 65535 px, so
    the large-image path does not fall back to scipy's serial EDT.
    """
    if _HAVE_NUMBA and max(mask.shape) > 65535:
        import numba
        return _nb_edt_band64(
            np.ascontiguousarray(mask, dtype=np.uint8),
            max(1, numba.get_num_threads()),
            np.float64(threshold), strict)
    dist = _edt32(mask)
    if strict:
        return ((dist > threshold)
                & (dist < np.float32(3.4028235e38))).astype(np.uint8)
    return (dist <= threshold).astype(np.uint8)


def _radius(size: float, edu: float = EDU) -> np.float32:
    """size*edu evaluated in float32, as in the C code."""
    return np.float32(np.float32(size) * np.float32(edu))


def getcore(im: np.ndarray, size: float, edu: float = EDU) -> np.ndarray:
    """
    getcore (mspa.c:208): foreground pixels whose distance to the foreground
    boundary is STRICTLY greater than size*edu.
    f_threshstrict(dist, size*edu, MIALFLOAT_MAX) -> size*edu < dist < MAX.
    """
    core = _threshold_edt(im.astype(bool), _radius(size, edu), strict=True)
    return core.astype(np.uint8)


def getexternalboundary(im: np.ndarray, size: float, edu: float = EDU) -> np.ndarray:
    """
    getexternalboundary (mspa.c:50): background pixels within distance size*edu
    of the foreground.
      nim = negation(im)             # background mask
      dist = sqrt(sqedt(nim))        # distance to nearest background pixel
      f_thresh(dist, 0, size*edu)    # 0 <= dist <= size*edu  (inclusive)
      AND nim                        # keep only background pixels
    """
    nim = (im == 0)                            # negation: background = 1
    band = _threshold_edt(nim, _radius(size, edu), strict=False) != 0
    return (band & nim).astype(np.uint8)


def rdil(mark: np.ndarray, mask: np.ndarray, graph: int) -> np.ndarray:
    """
    Reconstruction by dilation (rdil, recons.c): grow mark under mask.
    Binary morphological reconstruction == ndi.binary_propagation.
    """
    out = _ndi.binary_propagation(mark.astype(bool), mask=mask.astype(bool),
                                  structure=_struct(graph))
    return out.astype(np.uint8)


def rero(mark: np.ndarray, mask: np.ndarray, graph: int) -> np.ndarray:
    """
    Reconstruction by erosion (rero, recons.c): the dual of rdil.
    rero(mark, mask) = NOT( rdil(NOT mark, NOT mask) ).
    """
    nmark = (mark == 0)
    nmask = (mask == 0)
    out = _ndi.binary_propagation(nmark, mask=nmask, structure=_struct(graph))
    return (~out).astype(np.uint8)


def getpatch(im: np.ndarray, size: float, graphfg: int, edu: float = EDU,
             core: np.ndarray | None = None) -> np.ndarray:
    """
    getpatch / islet (mspa.c:224):
      core = getcore(im, size)
      core = rdil(core, im, graphfg)   # reconstruct the core-bearing patches
      return im - core                 # islets = foreground w/o any core
    (SUBSWAP_op computes im - core.)

    ``core`` may be supplied if ``getcore(im, size, edu)`` was already computed
    elsewhere, to avoid a redundant (expensive) distance transform. When given
    it must equal ``getcore(im, size, edu)`` exactly; the result is identical.
    """
    if core is None:
        core = getcore(im, size, edu)
    core = rdil(core, im, graphfg)
    islet = (im.astype(bool) & ~core.astype(bool))
    return islet.astype(np.uint8)


@njit(cache=True)
def _nb_fillhole(im, graph, q):
    """Fill background components that do not touch the image border.

    ``q`` is allocated by the caller with int32 indices for images up to
    2**31 pixels and int64 above that.  The flat pixel index no longer fits in
    int32 on the large maps this module supports.
    """
    ny, nx = im.shape
    reached = np.zeros((ny, nx), dtype=np.uint8)
    head = 0
    tail = 0

    # Seed all border zeros (they are connected to the outside).
    for x in range(nx):
        if im[0, x] == 0:
            reached[0, x] = 1
            q[tail] = x
            tail += 1
        if ny > 1 and im[ny - 1, x] == 0 and reached[ny - 1, x] == 0:
            reached[ny - 1, x] = 1
            q[tail] = (ny - 1) * nx + x
            tail += 1
    for y in range(1, ny - 1):
        if im[y, 0] == 0:
            reached[y, 0] = 1
            q[tail] = y * nx
            tail += 1
        if nx > 1 and im[y, nx - 1] == 0 and reached[y, nx - 1] == 0:
            reached[y, nx - 1] = 1
            q[tail] = y * nx + nx - 1
            tail += 1

    if graph == 8:
        dys = np.array([-1, 0, 1, 0, -1, -1, 1, 1], dtype=np.int64)
        dxs = np.array([0, -1, 0, 1, -1, 1, -1, 1], dtype=np.int64)
        nngb = 8
    else:
        dys = np.array([-1, 0, 1, 0], dtype=np.int64)
        dxs = np.array([0, -1, 0, 1], dtype=np.int64)
        nngb = 4

    while head < tail:
        idx = q[head]
        head += 1
        y = idx // nx
        x = idx - y * nx
        for k in range(nngb):
            yy = y + dys[k]
            xx = x + dxs[k]
            if 0 <= yy < ny and 0 <= xx < nx:
                if im[yy, xx] == 0 and reached[yy, xx] == 0:
                    reached[yy, xx] = 1
                    q[tail] = yy * nx + xx
                    tail += 1

    out = im.copy()
    for y in range(ny):
        for x in range(nx):
            if im[y, x] == 0 and reached[y, x] == 0:
                out[y, x] = 1
    return out


def uc_fillhole(im: np.ndarray, graph: int) -> np.ndarray:
    """
    uc_fillhole (mspa.c:239): morphological hole filling.
      marker = blank(255); frame set to 0; marker = sup(marker, im)
      return rero(marker, im, graph)
    Equivalent to ndi.binary_fill_holes with the given connectivity.
    """
    if _HAVE_NUMBA:
        data = np.ascontiguousarray(im, dtype=np.uint8)
        npix = data.size
        qdtype = np.int32 if npix <= 2 ** 31 else np.int64
        q = np.empty(npix, dtype=qdtype)
        return _nb_fillhole(data, graph, q)
    return _ndi.binary_fill_holes(im.astype(bool),
                                  structure=_struct(graph)).astype(np.uint8)


def setedges(im: np.ndarray, size: float, graphfg: int, graphbg: int,
             edu: float = EDU, core: np.ndarray | None = None,
             progress=None):
    """
    setedges (mspa.c:256): returns (edges, perforation).

    Mirrors the iterative peeling in the C code:
      core     = getcore(im, size)
      core1    = copy(core)
      corefill = fillhole(core, graphbg)
      loecher  = corefill - core
      edges    = 0
      loop:
        if volume(corefill)==0: break
        crt_edges = getexternalboundary(corefill, size)
        edges |= crt_edges
        i0 = fillhole(loecher, graphfg)
        corefill -= i0
        core -= corefill
        corefill = fillhole(core, graphbg)
        loecher = corefill - core
      outer = getexternalboundary(core1, size)
      edges &= negation(core1)          # external boundary only
      perforation = outer - edges
    """
    im_b = im.astype(bool)
    if core is None:
        core = getcore(im, size, edu).astype(bool)
    else:
        core = core.astype(bool)
    core1 = core.copy()

    corefill = uc_fillhole(core, graphbg).astype(bool)
    loecher = corefill & ~core
    edges = np.zeros_like(im_b)

    iteration = 0
    while True:
        if not corefill.any():                 # volume(corefill) == 0
            break
        iteration += 1
        if progress is not None:
            progress(f"setedges iteration {iteration}")
        crt_edges = getexternalboundary(corefill.astype(np.uint8), size, edu).astype(bool)
        edges |= crt_edges
        i0 = uc_fillhole(loecher.astype(np.uint8), graphfg).astype(bool)
        corefill = corefill & ~i0              # corefill -= i0
        core = core & ~corefill                # core -= corefill
        corefill = uc_fillhole(core.astype(np.uint8), graphbg).astype(bool)
        loecher = corefill & ~core

    if progress is not None:
        progress(f"setedges finished after {iteration} iterations")
    outer = getexternalboundary(core1.astype(np.uint8), size, edu).astype(bool)
    edges &= ~core1                            # bitwise AND negation(core1)
    perforation = outer & ~edges               # outer - edges

    return edges.astype(np.uint8), perforation.astype(np.uint8)


# ===========================================================================
# Preprocessing (fm_preproc, fm_preproc2), frame handling, sizing
# ===========================================================================


def compute_size(edge_width: float) -> np.float32:
    """size = (edge_width + 0.98) / sqrt(2)   (mspa.c:454), stored as float."""
    ew = np.float32(edge_width)
    return np.float32((float(ew) + 0.98) / float(np.sqrt(2.0)))


def compute_bufsize(size: float) -> int:
    """bufsize = (int)((size * 1.5) + 0.5)   (mspa.c:479)."""
    return int(float(size) * 1.5 + 0.5)


def addframebox(im: np.ndarray, w: int, value: int = 0) -> np.ndarray:
    """generic_addframebox with equal width w on all four sides, filled value."""
    return np.pad(im, ((w, w), (w, w)), mode="constant", constant_values=value)


def subframebox(im: np.ndarray, w: int) -> np.ndarray:
    """Inverse of addframebox: strip a frame of width w on all sides."""
    return im[w:im.shape[0] - w, w:im.shape[1] - w]


def fm_preproc(fm: np.ndarray, size: float, edu: float = EDU) -> np.ndarray:
    """
    fm_preproc (mspa.c:69). Input fm: 0=nodata, 1=background, 2=foreground.

      da  = (fm >= 1)                       # data mask (bg or fg)
      dda = getexternalboundary(da, size)   # data external boundary (into nodata)
      fa  = (fm >= 2)                       # foreground mask
      dfa = getexternalboundary(fa, size)   # fg external boundary
      dfa = dfa OR fa
      dfa = dfa AND dda      (INF_op = min, binary -> and)
      dfa = dfa OR  fa       (SUP_op = max, binary -> or)
      return dfa
    """
    da = (fm >= 1).astype(np.uint8)
    dda = getexternalboundary(da, size, edu).astype(bool)
    fa = (fm >= 2).astype(bool)
    dfa = getexternalboundary(fa.astype(np.uint8), size, edu).astype(bool)
    dfa = dfa | fa
    dfa = dfa & dda
    dfa = dfa | fa
    return dfa.astype(np.uint8)


def fm_preproc2(im: np.ndarray, size: int) -> np.ndarray:
    """
    fm_preproc2 (mspa.c:96): add a frame of width ``size`` and propagate each
    image border outward into the frame to mitigate border effects.

    A zero frame is added, then each border row/column is replicated outward
    into its side strip (left/right strips span only the original height;
    top/bottom strips span only the original width). The four corner
    quadrants are filled with the corresponding image corner value, exactly
    as in the C implementation. This matters when foreground touches an
    image corner: without it those corner regions are eroded to edge in the
    framed image, which changes a few border pixels of the final result.
    """
    ny, nx = im.shape
    out = np.zeros((ny + 2 * size, nx + 2 * size), dtype=im.dtype)
    # original image in the centre
    out[size:size + ny, size:size + nx] = im
    # left/right strips: replicate the border column across `size` cols,
    # over the original row span only (rows [size, size+ny)).
    left_col = im[:, 0][:, None]            # (ny,1)
    right_col = im[:, nx - 1][:, None]
    out[size:size + ny, 0:size] = np.repeat(left_col, size, axis=1)
    out[size:size + ny, size + nx:] = np.repeat(right_col, size, axis=1)
    # top/bottom strips: replicate the border row across `size` rows,
    # over the original column span only (cols [size, size+nx)).
    top_row = im[0, :][None, :]             # (1,nx)
    bot_row = im[ny - 1, :][None, :]
    out[0:size, size:size + nx] = np.repeat(top_row, size, axis=0)
    out[size + ny:, size:size + nx] = np.repeat(bot_row, size, axis=0)
    # corner quadrants: filled with the corresponding image corner value (mspa.c)
    out[0:size, 0:size] = im[0, 0]
    out[0:size, size + nx:] = im[0, nx - 1]
    out[size + ny:, 0:size] = im[ny - 1, 0]
    out[size + ny:, size + nx:] = im[ny - 1, nx - 1]
    return out


# ===========================================================================
# Connector generation: getconnector2core + geodesic external boundary (ced)
# ===========================================================================


def _inmaskp(pm, ncol, offset_r, x2, y2):
    """
    Port of inmaskp (ced.c:38): Bresenham line-of-sight from the reference
    pixel at flat index ``offset_r`` stepping toward relative (x2, y2).
    Returns False if any pixel along the straight line is outside the mask
    (pm == 0), True otherwise. pm is the flat mask buffer.
    """
    x2 = int(x2); y2 = int(y2)
    dx = abs(x2); dy = abs(y2)
    s1 = (1 if x2 > 0 else (-1 if x2 < 0 else 0))
    s2 = (1 if y2 > 0 else (-1 if y2 < 0 else 0))
    interchange = True
    if dy > dx:
        dx, dy = dy, dx
    else:
        interchange = False
    d = 2 * dy - dx
    incr1 = 2 * dy
    incr2 = 2 * (dy - dx)
    x = 0; y = 0
    der = dx
    while abs(y if interchange else x) < der:
        if interchange:
            y += s2
        else:
            x += s1
        if d < 0:
            d += incr1
        else:
            d += incr2
            if interchange:
                x += s1
            else:
                y += s2
        if pm[offset_r + x + y * ncol] == 0:
            return False
    return True


_SHORT_MAX = 32767


@njit(cache=True)
def _nb_inmaskp(pm, ncol, offset_r, x2, y2):
    """Numba port of inmaskp (ced.c:38)."""
    dx = abs(x2)
    dy = abs(y2)
    s1 = 1 if x2 > 0 else (-1 if x2 < 0 else 0)
    s2 = 1 if y2 > 0 else (-1 if y2 < 0 else 0)
    interchange = True
    if dy > dx:
        dx, dy = dy, dx
    else:
        interchange = False
    d = 2 * dy - dx
    incr1 = 2 * dy
    incr2 = 2 * (dy - dx)
    x = 0
    y = 0
    der = dx
    while abs(y if interchange else x) < der:
        if interchange:
            y += s2
        else:
            x += s1
        if d < 0:
            d += incr1
        else:
            d += incr2
            if interchange:
                x += s1
            else:
                y += s2
        if pm[offset_r + x + y * ncol] == 0:
            return False
    return True


@njit(cache=True)
def _q_add(q, qhead, qtail, qcount, value):
    """Append to a growable FIFO (logical order preserved)."""
    if qcount == q.size:
        old_size = q.size
        nq = np.concatenate((q, q))
        for j in range(qcount):
            nq[j] = q[(qhead + j) % old_size]
        q = nq
        qhead = 0
        qtail = qcount
    q[qtail] = value
    qtail = (qtail + 1) % q.size
    qcount += 1
    return q, qhead, qtail, qcount


@njit(cache=True)
def _q_remove(q, qhead, qtail, qcount):
    if qcount == 0:
        return 0, qhead, qcount
    value = q[qhead]
    qhead = (qhead + 1) % q.size
    qcount -= 1
    return value, qhead, qcount


@njit(cache=True)
def _q_look(q, qlook, qtail):
    if qlook == qtail:
        return 0, qlook
    value = q[qlook]
    qlook = (qlook + 1) % q.size
    return value, qlook


@njit(cache=True)
def _nb_ced(pr, pm, nx, ny, q):
    """Numba implementation of miallib's ced().

    pr/pm are flat uint8 arrays of the one-pixel-framed reference/mask.
    px/py are int16 vectors, pb and the result are float32, matching ced.c.
    The queue preserves FIFO order (a list was used in the pure-Python port).
    """
    npix = nx * ny
    shft = np.empty(4, dtype=np.int64)
    shft[0] = -nx
    shft[1] = -1
    shft[2] = nx
    shft[3] = 1
    dxs = np.array([0, 1, 0, -1], dtype=np.int64)
    dys = np.array([1, 0, -1, 0], dtype=np.int64)

    qhead = 0
    qtail = 0
    qcount = 0
    qlook = 0
    FICT = -1
    SHORT_MAX = 32767
    FLOAT_MAX = np.float32(3.4028235e38)

    # init: reference-and-mask pixels become seeds (pm=3); their non-ref mask
    # neighbours enter the queue.
    for i in range(npix):
        if pr[i] != 0 and pm[i] != 0:
            pm[i] = 3
            for k in range(4):
                ofsk = i + shft[k]
                if 0 <= ofsk < npix and pm[ofsk] == 1 and pr[ofsk] == 0:
                    q, qhead, qtail, qcount = _q_add(q, qhead, qtail, qcount, ofsk)
                    pm[ofsk] = 2

    px = np.full(npix, SHORT_MAX, dtype=np.int16)
    py = np.full(npix, SHORT_MAX, dtype=np.int16)
    pb = np.zeros(npix, dtype=np.float32)
    for i in range(npix):
        if pm[i] == 3:
            px[i] = 0
            py[i] = 0

    while qcount > 0:
        q, qhead, qtail, qcount = _q_add(q, qhead, qtail, qcount, FICT)
        dmin = FLOAT_MAX
        qlook = qhead
        while True:
            ofs, qlook = _q_look(q, qlook, qtail)
            if ofs == FICT:
                break
            if px[ofs] != SHORT_MAX:
                continue
            dp = FLOAT_MAX
            for k in range(4):
                ofsk = ofs + shft[k]
                if 0 <= ofsk < npix and px[ofsk] != SHORT_MAX:
                    bx = int(px[ofsk])
                    by = int(py[ofsk])
                    origin = ofsk - bx - by * nx
                    if _nb_inmaskp(pm, nx, origin, bx + dxs[k], by + dys[k]):
                        base_flag = False
                        dcrt = np.float32(
                            np.sqrt(np.float64((bx + dxs[k]) ** 2 + (by + dys[k]) ** 2))
                            + np.float64(pb[ofsk])
                        )
                    else:
                        base_flag = True
                        dcrt = np.float32(
                            np.sqrt(np.float64(bx * bx + by * by))
                            + np.float64(pb[ofsk]) + 1.0
                        )
                    if dcrt < dp:
                        if base_flag:
                            px[ofs] = dxs[k]
                            py[ofs] = dys[k]
                            pb[ofs] = np.float32(dcrt - 1.0)
                        else:
                            px[ofs] = bx + dxs[k]
                            py[ofs] = by + dys[k]
                            pb[ofs] = pb[ofsk]
                        dp = dcrt
            if dmin > dp:
                dmin = dp
        while True:
            ofs, qhead, qcount = _q_remove(q, qhead, qtail, qcount)
            if ofs == FICT:
                break
            if (np.sqrt(np.float64(int(px[ofs]) ** 2 + int(py[ofs]) ** 2))
                    + np.float64(pb[ofs]) > np.float64(dmin)):
                q, qhead, qtail, qcount = _q_add(q, qhead, qtail, qcount, ofs)
            else:
                for k in range(4):
                    ofsk = ofs + shft[k]
                    if 0 <= ofsk < npix and pm[ofsk] == 1:
                        q, qhead, qtail, qcount = _q_add(q, qhead, qtail, qcount, ofsk)
                        pm[ofsk] = 2

    out = np.zeros(npix, dtype=np.float32)
    for i in range(npix):
        if px[i] != SHORT_MAX:
            out[i] = np.float32(
                np.sqrt(np.float64(int(px[i]) ** 2 + int(py[i]) ** 2))
                + np.float64(pb[i])
            )
    return out


def ced(ref: np.ndarray, mask: np.ndarray) -> np.ndarray:
    """
    Constrained geodesic Euclidean distance (port of ced, ced.c:105).

    For every mask pixel, the Euclidean distance to the nearest reference
    pixel, measured along straight lines that stay inside the mask (geodesic
    constraint via the Bresenham line-of-sight test ``inmaskp``), with a +1
    geodesic fallback when the straight line is blocked.

    ref, mask: 2D uint8 binary (reference pixels / geodesic mask). Returns a
    float32 distance image (0 where not reached / ref), matching the C image
    type. ced.c uses a one-pixel frame (BOX_2D) and keeps the distance
    accumulator ``pb`` in float32; both details affect threshold ties.
    """
    ny0, nx0 = ref.shape
    F = 1  # ced.c uses BOX_2D (a one-pixel frame)
    nx = nx0 + 2 * F
    ny = ny0 + 2 * F
    pr = np.zeros((ny, nx), dtype=np.uint8)
    pm = np.zeros((ny, nx), dtype=np.uint8)
    pr[F:F + ny0, F:F + nx0] = (ref > 0).astype(np.uint8)
    pm[F:F + ny0, F:F + nx0] = (mask > 0).astype(np.uint8)
    npix = nx * ny
    qdtype = np.int32 if npix <= 2 ** 31 else np.int64
    cap = npix // 100 + 1024
    q = np.empty(cap, dtype=qdtype)
    out = _nb_ced(pr.ravel(), pm.ravel(), nx, ny, q)
    return out.reshape(ny, nx)[F:F + ny0, F:F + nx0].astype(np.float32)


def getexternalboundarygeodesic(im: np.ndarray, mask: np.ndarray,
                                size: float, edu: float = EDU) -> np.ndarray:
    """
    getexternalboundarygeodesic (mspa.c:321):
        thresh(ced(im, mask), 0.0001, size*edu, 0, 1)
    i.e. mask pixels whose GEODESIC (within-mask) Euclidean distance to the
    reference set ``im`` lies in (0.0001, size*edu].
    """
    radius = _radius(size, edu)
    if radius < np.float32(1.0):
        # smallest nonzero grid distance is 1.0 -> empty band
        return np.zeros(im.shape, dtype=np.uint8)
    dist = ced(im.astype(np.uint8), mask.astype(np.uint8)).astype(np.float32)
    band = (dist > np.float32(0.0001)) & (dist <= radius)
    return band.astype(np.uint8)


def getconnector2core(core: np.ndarray, opening: np.ndarray, residues: np.ndarray,
                      size: float, oitype: int, graphfg: int,
                      edu: float = EDU) -> np.ndarray:
    """
    getconnector2core (mspa.c:333). Returns the connector mask.

      sk = (opening | residues)
      binOIthin_FIFO(sk, oitype, atype=1, anchor=core)   # anchored thinning
      sk = sk - core
      i0 = (opening - core) | residues
      connector = getexternalboundarygeodesic(sk, i0, size-1)
      connector = connector | sk
      connector = connector & i0
      sk_rec = rdil(sk, i0, graphfg)
      connector = connector & sk_rec
    """
    core_b = core.astype(bool).copy()
    # binOIthin_FIFO() frames its anchor in place; reproduce the resulting
    # zeroed outer two-pixel border for the calculations that follow.
    core_b[:2, :] = False
    core_b[-2:, :] = False
    core_b[:, :2] = False
    core_b[:, -2:] = False
    opening_b = opening.astype(bool)
    residues_b = residues.astype(bool)

    sk = (opening_b | residues_b).astype(np.uint8)
    sk = binOIthin_FIFO(sk, oitype, core_b.astype(np.uint8))
    sk = (sk.astype(bool) & ~core_b)                     # sk - core

    i0 = (opening_b & ~core_b) | residues_b              # (opening - core) | residues

    connector = getexternalboundarygeodesic(sk.astype(np.uint8), i0.astype(np.uint8),
                                            np.float32(size - 1.0), edu).astype(bool)
    connector = connector | sk
    connector = connector & i0
    sk_rec = rdil(sk.astype(np.uint8), i0.astype(np.uint8), graphfg).astype(bool)
    connector = connector & sk_rec
    return connector.astype(np.uint8)


# ===========================================================================
# Bridge test: getcorridor
# ===========================================================================
from collections import deque as _deque


@njit(cache=True)
def _rq_add(q, head, tail, count, value):
    """Append to a growable integer ring queue, preserving FIFO order."""
    if count == q.size:
        old = q.size
        nq = np.concatenate((q, q))
        for j in range(count):
            nq[j] = q[(head + j) % old]
        q = nq
        head = 0
        tail = count
    q[tail] = value
    tail = (tail + 1) % q.size
    count += 1
    return q, head, tail, count


@njit(cache=True)
def _rq_remove(q, head, tail, count):
    if count == 0:
        return 0, head, tail, count
    value = q[head]
    head = (head + 1) % q.size
    count -= 1
    return value, head, tail, count


@njit(cache=True)
def _nb_wsfah(out, domain_flat, H, W, graph, q):
    """Numba core for miallib wsfah, operating in place on one label image.

    ``out`` holds core labels (0 = to flood).  Seeded pixels are temporarily
    marked with ``LABEL_MSB`` exactly as in uc_u32_wsfah, so a single int32
    image is enough; the MSB is cleared before the FIFO flood.  ``domain_flat``
    is the flood domain (1) / wall (0).  The queue is a growable ring buffer
    holding only the live frontier.
    """
    n = H * W
    cap = 4096 if n > 4096 else (n if n > 0 else 1)
    q = np.empty(cap, dtype=np.int64)
    head = 0
    tail = 0
    count = 0
    LABEL_MSB = 1 << 30

    dy4 = np.array([-1, 0, 1, 0], dtype=np.int64)   # N, W, S, E (set_shift)
    dx4 = np.array([0, -1, 0, 1], dtype=np.int64)
    dyd = np.array([-1, -1, 1, 1], dtype=np.int64)
    dxd = np.array([-1, 1, -1, 1], dtype=np.int64)

    # seeding: 4-connected pass, then diagonal pass (graph==8)
    for pass_id in range(2):
        if pass_id == 0:
            dyv = dy4
            dxv = dx4
        else:
            if graph != 8:
                continue
            dyv = dyd
            dxv = dxd
        for idx in range(n):
            lab = out[idx]
            if lab <= 0 or lab >= LABEL_MSB:
                continue
            y = idx // W
            x = idx - y * W
            for t in range(dyv.shape[0]):
                nyy = y + dyv[t]
                nxx = x + dxv[t]
                if nyy < 0 or nyy >= H or nxx < 0 or nxx >= W:
                    continue
                nk = nyy * W + nxx
                if out[nk] == 0 and domain_flat[nk] != 0:
                    out[nk] = lab | LABEL_MSB
                    q, head, tail, count = _rq_add(q, head, tail, count, nk)

    # clear the MSB used to protect seeded pixels during the two seed passes
    for i in range(n):
        if out[i] >= LABEL_MSB:
            out[i] ^= LABEL_MSB

    if graph == 8:
        dya = np.array([-1, 0, 1, 0, -1, -1, 1, 1], dtype=np.int64)
        dxa = np.array([0, -1, 0, 1, -1, 1, -1, 1], dtype=np.int64)
    else:
        dya = dy4
        dxa = dx4

    while count > 0:
        idx, head, tail, count = _rq_remove(q, head, tail, count)
        lab = out[idx]
        y = idx // W
        x = idx - y * W
        for t in range(dya.shape[0]):
            nyy = y + dya[t]
            nxx = x + dxa[t]
            if nyy < 0 or nyy >= H or nxx < 0 or nxx >= W:
                continue
            nk = nyy * W + nxx
            if out[nk] == 0 and domain_flat[nk] != 0:
                out[nk] = lab
                q, head, tail, count = _rq_add(q, head, tail, count, nk)


def _wsfah(labels: np.ndarray, domain: np.ndarray, graph: int,
           copy: bool = True) -> np.ndarray:
    """
    Flat-landscape watershed-from-markers by ordered FIFO flooding, a faithful
    port of miallib ``wsfah`` (wsfah.c) as invoked by getcorridor.

    getcorridor calls ``wsfah(lbl, imref, graph, 254)`` where the reference
    image ``imref`` is 0 on the flood domain ``(opening|connector)`` and 255
    (a wall) elsewhere. With a flat (all-zero) reference the immersion reduces
    to a single level-0 breadth-first flood: every core label grows outward
    into the domain one ring at a time, and the FIFO order makes the result
    first-come-first-served (a filled pixel is never relabelled).

    Reproducing *this* flood rather than a generic distance watershed matters:
    scipy/skimage break plateau ties by distance, which can hand a whole thin
    connector to a single core and collapse the bridge test to a loop. The
    miallib flood instead lets both cores reach the connector, so a 1-px neck
    between two distinct cores keeps labels from both (-> bridge). See the
    neighbour ordering in set_shift (setshft.c) and the 4-then-8 seeding of
    uc_u32_wsfah: for graph==8 the 4-connected neighbours of every marker are
    seeded first, the diagonals second, in N, W, S, E order.  wsfah() also
    zeroes the outer one-pixel border of the label image before and after the
    flood; that border is reproduced here.

    Parameters
    ----------
    labels : int array, core-object ids (0 = unlabelled / to be flooded).
    domain : bool array, pixels that may be flooded (walls are outside it).
    graph  : 4 or 8 foreground connectivity.

    Returns the flooded label image (same dtype semantics as ``labels``).
    """
    H, W = labels.shape
    if copy:
        labels = labels.astype(np.int32).copy()
    else:
        labels = np.asarray(labels, dtype=np.int32)
        if not labels.flags.c_contiguous:
            labels = np.ascontiguousarray(labels)

    # wsfah() frames the label image with a wall before seeding: border pixels
    # are neither markers nor floodable.  The MSB scheme lets the flood use the
    # same image, so no second label array is allocated.
    labels[0, :] = -1
    labels[-1, :] = -1
    labels[:, 0] = -1
    labels[:, -1] = -1
    labels_flat = labels.ravel()
    domain = np.asarray(domain)
    if not domain.flags.c_contiguous:
        domain = np.ascontiguousarray(domain)
    domain_flat = domain.ravel()

    n = H * W
    qdtype = np.int32 if n <= 2 ** 31 else np.int64
    cap = 4096 if n > 4096 else (n if n > 0 else 1)
    q = np.empty(cap, dtype=qdtype)
    _nb_wsfah(labels_flat, domain_flat, H, W, graph, q)

    out = labels_flat.reshape(H, W)
    out[out < 0] = 0
    out[0, :] = 0
    out[-1, :] = 0
    out[:, 0] = 0
    out[:, -1] = 0
    return out


def _wsfah_py_reference(labels: np.ndarray, domain: np.ndarray, graph: int) -> np.ndarray:
    """Pure-Python reference implementation of _wsfah (kept for validation /
    documentation; the production path uses the numba core _nb_wsfah)."""
    H, W = labels.shape
    out = labels.astype(np.int64).copy()
    out[~domain.astype(bool)] = -1              # walls: never floodable

    # neighbour offsets in miallib set_shift order (setshft.c)
    if graph == 8:
        # 4-connected first, diagonals second (matches uc_u32_wsfah seeding)
        n4 = [(-1, 0), (0, -1), (1, 0), (0, 1)]   # N, W, S, E
        nd = [(-1, -1), (-1, 1), (1, -1), (1, 1)]
    else:
        n4 = [(-1, 0), (0, -1), (1, 0), (0, 1)]   # N, W, S, E
        nd = []
    alln = n4 + nd

    q = _deque()
    seeded = np.zeros((H, W), dtype=bool)

    def _seed_pass(offsets):
        # raster-order scan over labelled markers; stamp 0-valued in-domain
        # neighbours with the marker's label and enqueue them.
        ys, xs = np.where(labels > 0)
        for y, x in zip(ys.tolist(), xs.tolist()):
            lab = int(labels[y, x])
            for dy, dx in offsets:
                ny, nx = y + dy, x + dx
                if 0 <= ny < H and 0 <= nx < W and out[ny, nx] == 0 \
                        and not seeded[ny, nx]:
                    out[ny, nx] = lab
                    seeded[ny, nx] = True
                    q.append((ny, nx))

    _seed_pass(n4)
    if nd:
        _seed_pass(nd)

    # level-0 breadth-first flood, first-come-first-served
    while q:
        y, x = q.popleft()
        lab = out[y, x]
        for dy, dx in alln:
            ny, nx = y + dy, x + dx
            if 0 <= ny < H and 0 <= nx < W and out[ny, nx] == 0:
                out[ny, nx] = lab
                q.append((ny, nx))

    out[out < 0] = 0
    return out


def getcorridor(connector: np.ndarray, core: np.ndarray, opening: np.ndarray,
                graphfg: int) -> np.ndarray:
    """
    getcorridor (mspa.c:392): the BRIDGE test.

      lbl   = label(core)                       # distinct core-object ids
      imref = (opening | connector), bg -> wall
      prop  = wsfah(lbl, imref, graph, 254)     # flood core labels over domain
      cor   = label(connector)                  # connector segments
      set_regions(cor, lbl=prop, 20)            # per-segment RANGE of core id
      corridor = (range >= 1)                   # segment spans >= 2 cores

    A connector segment whose flooded core-ids span >= 2 distinct cores joins
    different objects (BRIDGE); a segment touching a single core only loops
    back to it (LOOP). The flood is miallib's ordered FIFO ``_wsfah`` (not a
    distance watershed) so a 1-px neck between two cores is correctly bridged.

    miallib ``label()`` zeroes the outer one-pixel border of its copy before
    labelling (the 3x3 structuring element's frame) and numbers components
    from 2.  The border clipping is reproduced; the absolute ids do not
    matter because only the per-segment id range is used.
    """
    core_b = core.astype(bool)
    conn_b = connector.astype(bool)
    opening_b = opening.astype(bool)
    st = _struct(graphfg)

    core_clip = alloc(core_b.shape, np.uint8, "core_clip")
    np.copyto(core_clip, core_b, casting="unsafe")
    core_clip[0, :] = 0
    core_clip[-1, :] = 0
    core_clip[:, 0] = 0
    core_clip[:, -1] = 0
    core_lbl = alloc(core_b.shape, np.int32, "core_lbl")
    result = _ndi.label(core_clip, structure=st, output=core_lbl)
    n_core = result if isinstance(result, int) else result[1]
    release(core_clip)
    del core_clip

    domain = alloc(core_b.shape, np.bool_, "domain")
    np.logical_or(opening_b, conn_b, out=domain)

    # wsfah: flood core labels across the domain (background is a wall)
    prop = _wsfah(core_lbl, domain, graphfg, copy=False)
    release(domain)
    del domain

    conn_clip = alloc(core_b.shape, np.uint8, "conn_clip")
    np.copyto(conn_clip, conn_b, casting="unsafe")
    conn_clip[0, :] = 0
    conn_clip[-1, :] = 0
    conn_clip[:, 0] = 0
    conn_clip[:, -1] = 0
    conn_lbl = alloc(core_b.shape, np.int32, "conn_lbl")
    result = _ndi.label(conn_clip, structure=st, output=conn_lbl)
    n_conn = result if isinstance(result, int) else result[1]
    release(conn_clip)
    del conn_clip
    if n_conn == 0:
        return np.zeros_like(core_b, dtype=np.uint8)

    # Per-connector-segment range of the underlying flooded core-id: a segment
    # spanning >= 2 distinct cores is a BRIDGE. Computed in a single pass with
    # labelled min/max over only the pixels that carry a core id (prop > 0),
    # instead of scanning the whole image once per segment (which is
    # O(segments x pixels) and dominates on large rasters).
    seg_ids = conn_lbl[conn_b]                 # segment label per connector px
    core_ids = prop[conn_b]                    # flooded core id per connector px
    valid = core_ids > 0
    seg_v = seg_ids[valid]
    val_v = core_ids[valid]

    seg_min = np.full(n_conn + 1, np.iinfo(np.int64).max, dtype=np.int64)
    seg_max = np.full(n_conn + 1, np.iinfo(np.int64).min, dtype=np.int64)
    # np.minimum.at / maximum.at perform the grouped reduction in one pass.
    np.minimum.at(seg_min, seg_v, val_v)
    np.maximum.at(seg_max, seg_v, val_v)

    # Segments with no core id at all keep the sentinels; guard the
    # subtraction so the resulting overflow cannot look like a range >= 1.
    has_ids = seg_max >= seg_min
    is_bridge = np.zeros(n_conn + 1, dtype=bool)
    is_bridge[has_ids] = (seg_max[has_ids] - seg_min[has_ids]) >= 1
    is_bridge[0] = False                       # label 0 is background
    if active_workspace() is None:
        # RAM path: avoid an extra full-size buffer.
        corridor = is_bridge[conn_lbl] & conn_b
        return corridor.astype(np.uint8)
    corridor_mask = alloc(core_b.shape, np.bool_, "corridor_mask")
    np.take(is_bridge, conn_lbl, out=corridor_mask)
    np.logical_and(corridor_mask, conn_b, out=corridor_mask)
    corridor = alloc(core_b.shape, np.uint8, "corridor")
    np.copyto(corridor, corridor_mask, casting="unsafe")
    release(corridor_mask)
    del corridor_mask
    return corridor


# ===========================================================================
# Driver: segmentBinaryPatterns (bit-plane assembly + transition/intext)
# ===========================================================================


def _dilate4(mask: np.ndarray) -> np.ndarray:
    """dilate4: 4-connected dilation by 1 (mspa.c size==1 refinement)."""
    return _ndi.binary_dilation(mask.astype(bool), structure=_struct(4))


def segment_binary_patterns(imin: np.ndarray, edge_width: float,
                            graphfg: int = 8, transition: int = 1,
                            internal: int = 1, n_jobs: int | None = None,
                            memmap_dir: str | None = None,
                            memmap_min_bytes: int = 64 * 1024 * 1024,
                            progress: bool = False) -> np.ndarray:
    """
    Pure-Python port of segmentBinaryPatterns (mspa.c).

    imin: uint8 with 0=nodata, 1=background, 2=foreground.
    Returns the MSPA class-coded uint8 image (same shape as imin).

    Bit planes (OR-ed into out, base FG=bit0): edge<<1, perf<<2, islet<<3,
    core<<4, corridor(bridge)<<5, shortcut(loop)<<6. transition only affects
    colouring (not codes). internal(intext) adds core-opening (100) and the
    internal-territory fill (220) plus the +100 internal twins.

    n_jobs : int or None
        Number of threads used by the parallel Numba EDT (and, without Numba,
        by the thread pool for the independent stages). None (default) uses
        all cores available to this session (``nproc``), capped at Numba's
        configured maximum; 1 forces a serial run. The result is identical
        regardless of ``n_jobs``.

    memmap_dir : path or None
        Optional temporary directory for memory-mapped working arrays.  This
        is a storage-only option: the algorithm and results are unchanged.
        It is intended for rasters that exceed RAM; ordinary rasters should
        leave it as None.
    memmap_min_bytes : int
        Arrays at least this large are mapped to files when memmap_dir is
        set (default: 64 MiB).  Set to 0 to map every workspace allocation
        (used by the parity tests).
    progress : bool
        Print the effective thread count and per-stage timings to stderr.
        Useful for long runs; the result is unchanged.
    """
    if memmap_dir is not None:
        with workspace_context(memmap_dir, memmap_min_bytes):
            return segment_binary_patterns(
                imin, edge_width, graphfg, transition, internal, n_jobs,
                progress=progress)
    imin = np.asarray(imin)
    if imin.dtype != np.uint8:
        imin = imin.astype(np.uint8)
    if n_jobs is None:
        n_jobs = available_cpus()
    n_jobs = max(1, int(n_jobs))

    started = time.perf_counter()

    def log(msg: str) -> None:
        if progress:
            print(f"MSPA: {msg} [{time.perf_counter() - started:.1f}s]",
                  file=sys.stderr, flush=True)

    max_threads = 0
    layer = "none"
    if _HAVE_NUMBA:
        import numba
        # Numba's maximum can be lower than the OS CPU count (affinity,
        # cgroup limits or NUMBA_NUM_THREADS).  Never request more than it
        # permits; a failed threading-layer setup falls back to one thread
        # rather than aborting the whole run.
        max_threads = int(getattr(numba.config, "NUMBA_NUM_THREADS", 0) or 0)
        layer = str(getattr(numba.config, "THREADING_LAYER", "unknown"))
        if max_threads > 0:
            n_jobs = min(n_jobs, max_threads)
        try:
            numba.set_num_threads(n_jobs)
        except (ValueError, RuntimeError):
            n_jobs = 1
            try:
                numba.set_num_threads(1)
            except Exception:
                pass
        if progress:
            log(f"{imin.size:,} pixels; {n_jobs} threads "
                f"(Numba max {max_threads or '?'}, layer {layer})")
    elif progress:
        log(f"{imin.size:,} pixels; numba unavailable, serial run")
    if progress:
        log("note: only the distance-transform stages are parallel; "
            "most stages run on one core")

    stage_total = 8
    stage_no = 0
    stage_t0 = started
    stage_name = ""

    def stage(name: str) -> None:
        """Start a numbered stage, reporting the duration of the previous."""
        nonlocal stage_no, stage_t0, stage_name
        now = time.perf_counter()
        if stage_no:
            log(f"[{stage_no}/{stage_total}] {stage_name} done in "
                f"{now - stage_t0:.1f}s")
        stage_no += 1
        stage_name = name
        stage_t0 = now
        log(f"[{stage_no}/{stage_total}] {name}")

    def end_stage() -> None:
        nonlocal stage_no, stage_t0
        if stage_no:
            log(f"[{stage_no}/{stage_total}] {stage_name} done in "
                f"{time.perf_counter() - stage_t0:.1f}s")
            stage_no = 0

    if graphfg == 8:
        graphbg, oitype = 4, 0
    else:
        graphbg, oitype = 8, 1

    edu = EDU
    size = compute_size(edge_width)
    if (size == 1) and (transition == 2):
        transition = 0
    bufsize = compute_bufsize(size)
    box = int(bufsize + 1.5)
    F = bufsize + 1

    # frame the input with nodata (0)
    stage("framing and preprocessing")
    im = addframebox(imin, box, 0)

    # main preprocessing (serial prerequisite for the independent stages)
    i0 = fm_preproc(im, size, edu)
    i0 = subframebox(i0, box)
    i0 = fm_preproc2(i0, F)
    i0b = i0.astype(bool)

    # ------------------------------------------------------------------
    # Mutually independent stages. Each reads only i0 (or im) and writes a
    # disjoint result, so they run concurrently on a shared array. The heavy
    # work is scipy.ndimage / numpy, which releases the GIL, so threads give
    # real parallelism without copying the (potentially huge) arrays. setedges
    # is the long pole (an internal serial fillhole loop), so overlapping it
    # with core/patch/holes is where the wall-clock time is recovered.
    # ------------------------------------------------------------------
    # core is a single distance transform; compute it once up front and reuse
    # it in getpatch, setedges and opening, saving two redundant full-image
    # EDTs.  The EDT is the main parallel kernel in the engine.
    stage("core distance transform")
    core = getcore(i0, size, edu).astype(bool)

    def _task_patch():
        return getpatch(i0, size, graphfg, edu, core=core.astype(np.uint8)).astype(bool)

    def _task_edges():
        e, p = setedges(i0, size, graphfg, graphbg, edu, core=core,
                        progress=log)
        return e.astype(bool), p.astype(bool)

    def _task_holes():
        # internal: all holes (uses size=1 preproc) at the box-framed resolution
        i0h = fm_preproc(im, 1, edu).astype(bool)
        return (uc_fillhole(i0h.astype(np.uint8), graphbg).astype(bool) & ~i0h)

    # With Numba the EDT itself is parallel, so run these stages sequentially
    # to avoid oversubscribing the CPU.  The pure-Python fallback keeps the
    # thread pool, where scipy releases the GIL.
    stage("patch / edges / holes")
    if (not _HAVE_NUMBA) and n_jobs > 1:
        with ThreadPoolExecutor(max_workers=n_jobs) as ex:
            f_edges = ex.submit(_task_edges)
            f_patch = ex.submit(_task_patch)
            f_holes = ex.submit(_task_holes) if internal == 1 else None
            patch = f_patch.result()
            edges, perf = f_edges.result()
            allHoles = f_holes.result() if f_holes is not None else None
    else:
        patch = _task_patch()
        edges, perf = _task_edges()
        allHoles = _task_holes() if internal == 1 else None

    # when box == F the allHoles frame already matches i0; otherwise reframe.
    if internal == 1 and allHoles.shape != i0b.shape:
        # strip box frame, re-add F frame (edge-extend not needed: holes are 0)
        ah = subframebox(allHoles.astype(np.uint8), box)
        allHoles = addframebox(ah, F, 0).astype(bool)

    # release the stage closures and the box-framed input: they hold full-size
    # references that are no longer needed from here on.
    del _task_patch, _task_edges, _task_holes
    del im

    stage("bit assembly / opening")
    out = i0.astype(np.uint8)                     # FG base = bit0 = 1
    del i0

    opening = (getexternalboundary(core.astype(np.uint8), size, edu).astype(bool) | core)

    # residues = i0 - core - patch - perforation - edges  (bit assembly)
    residues = i0b & ~core
    residues = residues & ~patch
    out = out | (patch.astype(np.uint8) << 3)     # islet = 8
    residues = residues & ~perf
    out = out | (perf.astype(np.uint8) << 2)      # perforation = 4
    residues = residues & ~edges
    out = out | (edges.astype(np.uint8) << 1)     # edge = 2
    del i0b, patch, perf, edges

    # binOIthin_FIFO() frames its anchor image in place (generic_framebox),
    # zeroing the outer two rows/columns of `core`; the C driver then uses
    # that modified core for the connector, bridge test and output bits.
    core_work = core.copy()
    core_work[:2, :] = False
    core_work[-2:, :] = False
    core_work[:, :2] = False
    core_work[:, -2:] = False

    stage("connector")
    connector = getconnector2core(core_work.astype(np.uint8), opening.astype(np.uint8),
                                  residues.astype(np.uint8), size, oitype, graphfg,
                                  edu).astype(bool)
    stage("corridor")
    corridor = getcorridor(connector.astype(np.uint8), core_work.astype(np.uint8),
                           opening.astype(np.uint8), graphfg).astype(bool)
    del opening

    out = out | (core_work.astype(np.uint8) << 4)  # core = 16
    shortcut = connector & ~corridor              # shortcut = connector - corridor
    del connector

    if size == 1:
        corridor = corridor | (_dilate4(corridor) & residues)
        shortcut = shortcut | (_dilate4(shortcut) & residues)

    out = out | (corridor.astype(np.uint8) << 5)  # bridge = 32
    out = out | (shortcut.astype(np.uint8) << 6)  # loop   = 64

    stage("internal fill")
    if internal == 1:
        tmp = uc_fillhole(core_work.astype(np.uint8), graphbg).astype(bool)
        coreHoles = tmp & ~core
        # allHoles = allHoles AND NOT coreHoles
        allHoles = allHoles & ~coreHoles
        allHoles_val = np.where(allHoles, 220, 0).astype(np.uint8)
        tmp = tmp & ~core_work                     # filled core-hole region
        tmp2 = uc_fillhole(tmp.astype(np.uint8), graphbg).astype(bool)
        tmp2_val = np.where(tmp2, 100, 0).astype(np.uint8)
        out = out + tmp2_val                       # +100 internal twins / core-opening
        out = np.maximum(out, allHoles_val)        # SUP with 220 regions

    stage("finalize")
    # crop frame
    out = subframebox(out, F)

    # final masking: nodata (imin==0) -> 129
    nodata = (imin == 0)
    out[nodata] = 129

    end_stage()
    log("done")
    return out.astype(np.uint8)
