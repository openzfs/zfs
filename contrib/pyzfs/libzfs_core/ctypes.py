# Copyright 2015 ClusterHQ. See LICENSE file for details.

"""
Utility functions for casting to a specific C type.
"""

from .bindings.libnvpair import ffi as _ffi


def _ffi_cast(type_name):
    type_info = _ffi.typeof(type_name)

    def _func(value):
        # this is for overflow / underflow checking only
        if type_info.kind == 'enum':
            try:
                type_info.elements[value]
            except KeyError as e:
                raise OverflowError('Invalid enum <%s> value %s' %
                                    (type_info.cname, e.message))
        else:
            _ffi.new(type_name + '*', value)
        return _ffi.cast(type_name, value)
    _func.__name__ = type_name
    return _func


uint8_t =       _ffi_cast('uint8_t')
int8_t =        _ffi_cast('int8_t')
uint16_t =      _ffi_cast('uint16_t')
int16_t =       _ffi_cast('int16_t')
uint32_t =      _ffi_cast('uint32_t')
int32_t =       _ffi_cast('int32_t')
uint64_t =      _ffi_cast('uint64_t')
int64_t =       _ffi_cast('int64_t')
boolean_t =     _ffi_cast('boolean_t')
uchar_t =       _ffi_cast('uchar_t')


# First element of the value tuple is a suffix for a single value function
# while the second element is for an array function
_type_to_suffix = {
    _ffi.typeof('uint8_t'):     ('uint8', 'uint8'),
    _ffi.typeof('int8_t'):      ('int8', 'int8'),
    _ffi.typeof('uint16_t'):    ('uint16', 'uint16'),
    _ffi.typeof('int16_t'):     ('int16', 'int16'),
    _ffi.typeof('uint32_t'):    ('uint32', 'uint32'),
    _ffi.typeof('int32_t'):     ('int32', 'int32'),
    _ffi.typeof('uint64_t'):    ('uint64', 'uint64'),
    _ffi.typeof('int64_t'):     ('int64', 'int64'),
    _ffi.typeof('boolean_t'):   ('boolean_value', 'boolean'),
    _ffi.typeof('uchar_t'):     ('byte', 'byte'),
}


# vim: softtabstop=4 tabstop=4 expandtab shiftwidth=4
