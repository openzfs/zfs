#
# Copyright 2015 ClusterHQ
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

"""
The package that contains a module per each C library that
`libzfs_core` uses.  The modules expose CFFI objects required
to make calls to functions in the libraries.
"""
from __future__ import absolute_import, division, print_function

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
        module = importlib.import_module("." + module_name, __name__)
        ffi.cdef(module.CDEF)
        lib = LazyLibrary(ffi, module.LIBRARY)
        setattr(module, "ffi", ffi)
        setattr(module, "lib", lib)


_setup_cffi()

# vim: softtabstop=4 tabstop=4 expandtab shiftwidth=4
