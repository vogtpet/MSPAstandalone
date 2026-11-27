"""CPU-count helpers shared by the engine and the preflight check.

The number of *usable* CPUs can be lower than the number of logical CPUs,
for example through ``OMP_NUM_THREADS``/``nproc`` or a scheduler allocation.
"""

from __future__ import annotations

import os
import subprocess
import sys


def system_cpus() -> int:
    """Number of logical CPUs reported by the operating system."""
    return max(1, os.cpu_count() or 1)


def available_cpus() -> int:
    """Number of CPUs usable in this session, preferring ``nproc``.

    On Linux this honours the same limit as the ``nproc`` command (for
    example an OpenMP/thread environment variable or a scheduler allocation).
    ``nproc`` does not exist on macOS/Windows, so those platforms report the
    OS logical CPU count; use an explicit ``-j`` to override.
    """
    if sys.platform.startswith("linux"):
        try:
            value = int(subprocess.check_output(
                ["nproc"], text=True, stderr=subprocess.DEVNULL).strip())
            if value > 0:
                return value
        except Exception:
            pass
        try:
            return max(1, len(os.sched_getaffinity(0)))  # type: ignore[attr-defined]
        except (AttributeError, OSError):
            pass
    return system_cpus()
