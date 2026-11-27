"""Out-of-core array allocation for the pure-Python MSPA engine.

The Workspace is a storage-only abstraction: arrays have the same dtype,
shape and values whether they are backed by RAM or by a memory-mapped file.
It is used only when the caller supplies a temporary directory; the default
path is unchanged and allocates ordinary NumPy arrays.
"""

from __future__ import annotations

import mmap
import os
import threading
from pathlib import Path

import numpy as np


class Workspace:
    def __init__(self, directory: str | os.PathLike[str] | None,
                 min_bytes: int = 64 * 1024 * 1024):
        self.directory = Path(directory) if directory is not None else None
        self.min_bytes = int(min_bytes)
        self._lock = threading.Lock()
        self._counter = 0
        self._paths: list[Path] = []
        self._arrays: list[np.memmap] = []
        self._path_by_id: dict[int, Path] = {}
        if self.directory is not None:
            self.directory.mkdir(parents=True, exist_ok=True)

    @property
    def enabled(self) -> bool:
        return self.directory is not None

    def _new_path(self, name: str) -> Path:
        with self._lock:
            self._counter += 1
            serial = self._counter
        safe = "".join(c if c.isalnum() or c in "-_" else "_" for c in name)
        return self.directory / f"{os.getpid()}_{serial:05d}_{safe}.dat"

    def empty(self, shape, dtype, name: str = "array") -> np.ndarray:
        dtype = np.dtype(dtype)
        count = int(np.prod(shape, dtype=np.int64))
        nbytes = count * dtype.itemsize
        if self.directory is not None and nbytes >= self.min_bytes:
            path = self._new_path(name)
            arr = np.memmap(path, dtype=dtype, mode="w+", shape=tuple(shape))
            self._paths.append(path)
            self._arrays.append(arr)
            self._path_by_id[id(arr)] = path
            return arr
        return np.empty(shape, dtype=dtype)

    def zeros(self, shape, dtype, name: str = "array") -> np.ndarray:
        arr = self.empty(shape, dtype, name)
        arr[...] = 0
        return arr

    def release(self, arr) -> None:
        """Drop one mapped array early (best effort).

        This is used for large temporaries that are no longer needed, so the
        OS can reclaim their pages before the workspace is closed.
        """
        path = self._path_by_id.pop(id(arr), None)
        if path is None:
            return
        try:
            arr.flush()
        except Exception:
            pass
        mm = getattr(arr, "_mmap", None)
        if mm is not None and hasattr(mm, "madvise"):
            try:
                mm.madvise(mmap.MADV_DONTNEED)
            except Exception:
                pass
        self._arrays = [a for a in self._arrays if a is not arr]
        try:
            self._paths.remove(path)
        except ValueError:
            pass
        try:
            path.unlink()
        except OSError:
            pass

    def flush(self) -> None:
        for arr in self._arrays:
            try:
                arr.flush()
            except Exception:
                pass

    def cleanup(self) -> None:
        self.flush()
        self._arrays.clear()
        self._path_by_id.clear()
        # On POSIX an open mapping can be removed; on Windows the file remains
        # until the mapping/process is closed.  Cleanup is best effort.
        for path in self._paths:
            try:
                path.unlink()
            except OSError:
                pass
        self._paths.clear()


class workspace_context:
    """Activate a Workspace for the duration of a block."""

    def __init__(self, directory, min_bytes: int = 64 * 1024 * 1024):
        self.directory = directory
        self.min_bytes = min_bytes
        self.workspace: Workspace | None = None
        self._previous = None

    def __enter__(self):
        global _ACTIVE
        self._previous = _ACTIVE
        if self.directory is not None:
            self.workspace = Workspace(self.directory, self.min_bytes)
            _ACTIVE = self.workspace
        return self.workspace

    def __exit__(self, exc_type, exc, tb):
        global _ACTIVE
        _ACTIVE = self._previous
        if self.workspace is not None:
            self.workspace.cleanup()
        return False


_ACTIVE: Workspace | None = None


def active_workspace() -> Workspace | None:
    return _ACTIVE


def release(arr) -> None:
    ws = _ACTIVE
    if ws is not None:
        ws.release(arr)


def alloc(shape, dtype, name: str = "array", zero: bool = False) -> np.ndarray:
    """Allocate through the active Workspace, or ordinary RAM."""
    ws = _ACTIVE
    if ws is None:
        return np.zeros(shape, dtype=dtype) if zero else np.empty(shape, dtype=dtype)
    return ws.zeros(shape, dtype, name) if zero else ws.empty(shape, dtype, name)
