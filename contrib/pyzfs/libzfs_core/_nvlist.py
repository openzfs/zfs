# SPDX-License-Identifier: Apache-2.0
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
nvlist_in and nvlist_out provide support for converting between
a dictionary on the Python side and an nvlist_t on the C side
with the automatic memory management for C memory allocations.

nvlist_in takes a dictionary and produces a CData object corresponding
to a C nvlist_t pointer suitable for passing as an input parameter.
The nvlist_t is populated based on the dictionary.

nvlist_out takes a dictionary and produces a CData object corresponding
to a C nvlist_t pointer to pointer suitable for passing as an output parameter.
Upon exit from a with-block the dictionary is populated based on the nvlist_t.

The dictionary must follow a certain format to be convertible
to the nvlist_t.  The dictionary produced from the nvlist_t
will follow the same format.

Format:
- keys are always byte strings
- a value can be None in which case it represents boolean truth by its mere
    presence
- a value can be a bool
- a value can be a byte string
- a value can be an integer
- a value can be a CFFI CData object representing one of the following C types:
    int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t, uint64_t,
    boolean_t, uchar_t
- a value can be a dictionary that recursively adheres to this format
- a value can be a list of bools, byte strings, integers or CData objects of
    types specified above
- a value can be a list of dictionaries that adhere to this format
- all elements of a list value must be of the same type
"""
from __future__ import absolute_import, division, print_function

import numbers
from collections import namedtuple
from contextlib import contextmanager
from .bindings import libnvpair
from .ctypes import _type_to_suffix

_ffi = libnvpair.ffi
_lib = libnvpair.lib


def nvlist_in(props):
    """
    This function converts a python dictionary to a C nvlist_t
    and provides automatic memory management for the latter.

    :param dict props: the dictionary to be converted.
    :return: an FFI CData object representing the nvlist_t pointer.
    :rtype: CData
    """
    nvlistp = _ffi.new("nvlist_t **")
    res = _lib.nvlist_alloc(nvlistp, 1, 0)  # UNIQUE_NAME == 1
    if res != 0:
        raise MemoryError('nvlist_alloc failed')
    nvlist = _ffi.gc(nvlistp[0], _lib.nvlist_free)
    _dict_to_nvlist(props, nvlist)
    return nvlist


@contextmanager
def nvlist_out(props):
    """
    A context manager that allocates a pointer to a C nvlist_t and yields
    a CData object representing a pointer to the pointer via 'as' target.
    The caller can pass that pointer to a pointer to a C function that
    creates a new nvlist_t object.
    The context manager takes care of memory management for the nvlist_t
    and also populates the 'props' dictionary with data from the nvlist_t
    upon leaving the 'with' block.

    :param dict props: the dictionary to be populated with data from the
        nvlist.
    :return: an FFI CData object representing the pointer to nvlist_t pointer.
    :rtype: CData
    """
    nvlistp = _ffi.new("nvlist_t **")
    nvlistp[0] = _ffi.NULL  # to be sure
    try:
        yield nvlistp
        # clear old entries, if any
        props.clear()
        _nvlist_to_dict(nvlistp[0], props)
    finally:
        if nvlistp[0] != _ffi.NULL:
            _lib.nvlist_free(nvlistp[0])
            nvlistp[0] = _ffi.NULL


def packed_nvlist_out(packed_nvlist, packed_size):
    """
    This function converts a packed C nvlist_t to a python dictionary and
    provides automatic memory management for the former.

    :param bytes packed_nvlist: packed nvlist_t.
    :param int packed_size: nvlist_t packed size.
    :return: an `dict` of values representing the data contained by nvlist_t.
    :rtype: dict
    """
    props = {}
    with nvlist_out(props) as nvp:
        ret = _lib.nvlist_unpack(packed_nvlist, packed_size, nvp, 0)
    if ret != 0:
        raise MemoryError('nvlist_unpack failed')
    return props


_TypeInfo = namedtuple('_TypeInfo', ['suffix', 'ctype', 'is_array', 'convert'])


def _type_info(typeid):
    return {
        _lib.DATA_TYPE_BOOLEAN:         _TypeInfo(None, None, None, None),
        _lib.DATA_TYPE_BOOLEAN_VALUE:   _TypeInfo("boolean_value", "boolean_t *", False, bool),  # noqa: E501
        _lib.DATA_TYPE_BYTE:            _TypeInfo("byte", "uchar_t *", False, int),  # noqa: E501
        _lib.DATA_TYPE_INT8:            _TypeInfo("int8", "int8_t *", False, int),  # noqa: E501
        _lib.DATA_TYPE_UINT8:           _TypeInfo("uint8", "uint8_t *", False, int),  # noqa: E501
        _lib.DATA_TYPE_INT16:           _TypeInfo("int16", "int16_t *", False, int),  # noqa: E501
        _lib.DATA_TYPE_UINT16:          _TypeInfo("uint16", "uint16_t *", False, int),  # noqa: E501
        _lib.DATA_TYPE_INT32:           _TypeInfo("int32", "int32_t *", False, int),  # noqa: E501
        _lib.DATA_TYPE_UINT32:          _TypeInfo("uint32", "uint32_t *", False, int),  # noqa: E501
        _lib.DATA_TYPE_INT64:           _TypeInfo("int64", "int64_t *", False, int),  # noqa: E501
        _lib.DATA_TYPE_UINT64:          _TypeInfo("uint64", "uint64_t *", False, int),  # noqa: E501
        _lib.DATA_TYPE_STRING:          _TypeInfo("string", "char **", False, _ffi.string),  # noqa: E501
        _lib.DATA_TYPE_NVLIST:          _TypeInfo("nvlist", "nvlist_t **", False, lambda x: _nvlist_to_dict(x, {})),  # noqa: E501
        _lib.DATA_TYPE_BOOLEAN_ARRAY:   _TypeInfo("boolean_array", "boolean_t **", True, bool),  # noqa: E501
        # XXX use bytearray ?
        _lib.DATA_TYPE_BYTE_ARRAY:      _TypeInfo("byte_array", "uchar_t **", True, int),  # noqa: E501
        _lib.DATA_TYPE_INT8_ARRAY:      _TypeInfo("int8_array", "int8_t **", True, int),  # noqa: E501
        _lib.DATA_TYPE_UINT8_ARRAY:     _TypeInfo("uint8_array", "uint8_t **", True, int),  # noqa: E501
        _lib.DATA_TYPE_INT16_ARRAY:     _TypeInfo("int16_array", "int16_t **", True, int),  # noqa: E501
        _lib.DATA_TYPE_UINT16_ARRAY:    _TypeInfo("uint16_array", "uint16_t **", True, int),  # noqa: E501
        _lib.DATA_TYPE_INT32_ARRAY:     _TypeInfo("int32_array", "int32_t **", True, int),  # noqa: E501
        _lib.DATA_TYPE_UINT32_ARRAY:    _TypeInfo("uint32_array", "uint32_t **", True, int),  # noqa: E501
        _lib.DATA_TYPE_INT64_ARRAY:     _TypeInfo("int64_array", "int64_t **", True, int),  # noqa: E501
        _lib.DATA_TYPE_UINT64_ARRAY:    _TypeInfo("uint64_array", "uint64_t **", True, int),  # noqa: E501
        _lib.DATA_TYPE_STRING_ARRAY:    _TypeInfo("string_array", "char ***", True, _ffi.string),  # noqa: E501
        _lib.DATA_TYPE_NVLIST_ARRAY:    _TypeInfo("nvlist_array", "nvlist_t ***", True, lambda x: _nvlist_to_dict(x, {})),  # noqa: E501
    }[typeid]


# only integer properties need to be here
_prop_name_to_type_str = {
    b"rewind-request":   "uint32",
    b"type":             "uint32",
    b"N_MORE_ERRORS":    "int32",
    b"pool_context":     "int32",
}


def _nvlist_add_array(nvlist, key, array):
    def _is_integer(x):
        return isinstance(x, numbers.Integral) and not isinstance(x, bool)

    ret = 0
    specimen = array[0]
    is_integer = _is_integer(specimen)
    specimen_ctype = None
    if isinstance(specimen, _ffi.CData):
        specimen_ctype = _ffi.typeof(specimen)

    for element in array[1:]:
        if is_integer and _is_integer(element):
            pass
        elif type(element) is not type(specimen):
            raise TypeError('Array has elements of different types: ' +
                            type(specimen).__name__ +
                            ' and ' +
                            type(element).__name__)
        elif specimen_ctype is not None:
            ctype = _ffi.typeof(element)
            if ctype is not specimen_ctype:
                raise TypeError('Array has elements of different C types: ' +
                                _ffi.typeof(specimen).cname +
                                ' and ' +
                                _ffi.typeof(element).cname)

    if isinstance(specimen, dict):
        # NB: can't use automatic memory management via nvlist_in() here,
        # we have a loop, but 'with' would require recursion
        c_array = []
        for dictionary in array:
            nvlistp = _ffi.new('nvlist_t **')
            res = _lib.nvlist_alloc(nvlistp, 1, 0)  # UNIQUE_NAME == 1
            if res != 0:
                raise MemoryError('nvlist_alloc failed')
            nested_nvlist = _ffi.gc(nvlistp[0], _lib.nvlist_free)
            _dict_to_nvlist(dictionary, nested_nvlist)
            c_array.append(nested_nvlist)
        ret = _lib.nvlist_add_nvlist_array(nvlist, key, c_array, len(c_array))
    elif isinstance(specimen, bytes):
        c_array = []
        for string in array:
            c_array.append(_ffi.new('char[]', string))
        ret = _lib.nvlist_add_string_array(nvlist, key, c_array, len(c_array))
    elif isinstance(specimen, bool):
        ret = _lib.nvlist_add_boolean_array(nvlist, key, array, len(array))
    elif isinstance(specimen, numbers.Integral):
        suffix = _prop_name_to_type_str.get(key, "uint64")
        cfunc = getattr(_lib, "nvlist_add_%s_array" % (suffix,))
        ret = cfunc(nvlist, key, array, len(array))
    elif isinstance(
            specimen, _ffi.CData) and _ffi.typeof(specimen) in _type_to_suffix:
        suffix = _type_to_suffix[_ffi.typeof(specimen)][True]
        cfunc = getattr(_lib, "nvlist_add_%s_array" % (suffix,))
        ret = cfunc(nvlist, key, array, len(array))
    else:
        raise TypeError('Unsupported value type ' + type(specimen).__name__)
    if ret != 0:
        raise MemoryError('nvlist_add failed, err = %d' % ret)


def _nvlist_to_dict(nvlist, props):
    pair = _lib.nvlist_next_nvpair(nvlist, _ffi.NULL)
    while pair != _ffi.NULL:
        name = _ffi.string(_lib.nvpair_name(pair))
        typeid = int(_lib.nvpair_type(pair))
        typeinfo = _type_info(typeid)
        is_array = bool(_lib.nvpair_type_is_array(pair))
        cfunc = getattr(_lib, "nvpair_value_%s" % (typeinfo.suffix,), None)
        val = None
        ret = 0
        if is_array:
            valptr = _ffi.new(typeinfo.ctype)
            lenptr = _ffi.new("uint_t *")
            ret = cfunc(pair, valptr, lenptr)
            if ret != 0:
                raise RuntimeError('nvpair_value failed')
            length = int(lenptr[0])
            val = []
            for i in range(length):
                val.append(typeinfo.convert(valptr[0][i]))
        else:
            if typeid == _lib.DATA_TYPE_BOOLEAN:
                val = None  # XXX or should it be True ?
            else:
                valptr = _ffi.new(typeinfo.ctype)
                ret = cfunc(pair, valptr)
                if ret != 0:
                    raise RuntimeError('nvpair_value failed')
                val = typeinfo.convert(valptr[0])
        props[name] = val
        pair = _lib.nvlist_next_nvpair(nvlist, pair)
    return props


def _dict_to_nvlist(props, nvlist):
    for k, v in props.items():
        if not isinstance(k, bytes):
            raise TypeError('Unsupported key type ' + type(k).__name__)
        ret = 0
        if isinstance(v, dict):
            ret = _lib.nvlist_add_nvlist(nvlist, k, nvlist_in(v))
        elif isinstance(v, list):
            _nvlist_add_array(nvlist, k, v)
        elif isinstance(v, bytes):
            ret = _lib.nvlist_add_string(nvlist, k, v)
        elif isinstance(v, bool):
            ret = _lib.nvlist_add_boolean_value(nvlist, k, v)
        elif v is None:
            ret = _lib.nvlist_add_boolean(nvlist, k)
        elif isinstance(v, numbers.Integral):
            suffix = _prop_name_to_type_str.get(k, "uint64")
            cfunc = getattr(_lib, "nvlist_add_%s" % (suffix,))
            ret = cfunc(nvlist, k, v)
        elif isinstance(v, _ffi.CData) and _ffi.typeof(v) in _type_to_suffix:
            suffix = _type_to_suffix[_ffi.typeof(v)][False]
            cfunc = getattr(_lib, "nvlist_add_%s" % (suffix,))
            ret = cfunc(nvlist, k, v)
        else:
            raise TypeError('Unsupported value type ' + type(v).__name__)
        if ret != 0:
            raise MemoryError('nvlist_add failed')


# vim: softtabstop=4 tabstop=4 expandtab shiftwidth=4
