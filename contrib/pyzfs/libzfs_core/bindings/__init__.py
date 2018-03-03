# Copyright 2015 ClusterHQ. See LICENSE file for details.

"""
The package that contains a module per each C library that
`libzfs_core` uses.  The modules expose CFFI objects required
to make calls to functions in the libraries.
"""

import threading
import importlib

from cffi import FFI


def _setup_cffi():
    class LazyLibrary(object):

        def __init__(self, ffi, libname):
            self._ffi = ffi
            self._libname = libname
            self._lib = None
            self._lock = threading.Lock()

        def __getattr__(self, name):
            if self._lib is None:
                with self._lock:
                    if self._lib is None:
                        self._lib = self._ffi.dlopen(self._libname)

            return getattr(self._lib, name)

    MODULES = ["libnvpair", "libzfs_core"]
    ffi = FFI()

    for module_name in MODULES:
        module = importlib.import_module("." + module_name, __package__)
        ffi.cdef(module.CDEF)
        lib = LazyLibrary(ffi, module.LIBRARY)
        setattr(module, "ffi", ffi)
        setattr(module, "lib", lib)


_setup_cffi()

# vim: softtabstop=4 tabstop=4 expandtab shiftwidth=4
