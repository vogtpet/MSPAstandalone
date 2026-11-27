"""Pure-Python Morphological Spatial Pattern Analysis (MSPA).

The public entry point is :func:`segment_binary_patterns`, a port of the
miallib ``segmentBinaryPatterns`` / ``fsp`` implementation used by the
standalone MSPA C program.  See ``python/README.md`` for details.
"""

from .engine import (
    binOIthin_FIFO,
    ced,
    compute_bufsize,
    compute_size,
    fm_preproc,
    fm_preproc2,
    getconnector2core,
    getcore,
    getcorridor,
    getexternalboundary,
    getpatch,
    segment_binary_patterns,
    setedges,
    uc_fillhole,
)

__all__ = [
    "segment_binary_patterns",
    "compute_size",
    "compute_bufsize",
    "getcore",
    "getpatch",
    "getexternalboundary",
    "setedges",
    "uc_fillhole",
    "getconnector2core",
    "getcorridor",
    "binOIthin_FIFO",
    "ced",
    "fm_preproc",
    "fm_preproc2",
]
