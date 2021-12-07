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
Python bindings for ``libnvpair``.
"""
from __future__ import absolute_import, division, print_function

CDEF = """
    typedef ... nvlist_t;
    typedef ... nvpair_t;


    typedef enum {
        DATA_TYPE_UNKNOWN = 0,
        DATA_TYPE_BOOLEAN,
        DATA_TYPE_BYTE,
        DATA_TYPE_INT16,
        DATA_TYPE_UINT16,
        DATA_TYPE_INT32,
        DATA_TYPE_UINT32,
        DATA_TYPE_INT64,
        DATA_TYPE_UINT64,
        DATA_TYPE_STRING,
        DATA_TYPE_BYTE_ARRAY,
        DATA_TYPE_INT16_ARRAY,
        DATA_TYPE_UINT16_ARRAY,
        DATA_TYPE_INT32_ARRAY,
        DATA_TYPE_UINT32_ARRAY,
        DATA_TYPE_INT64_ARRAY,
        DATA_TYPE_UINT64_ARRAY,
        DATA_TYPE_STRING_ARRAY,
        DATA_TYPE_HRTIME,
        DATA_TYPE_NVLIST,
        DATA_TYPE_NVLIST_ARRAY,
        DATA_TYPE_BOOLEAN_VALUE,
        DATA_TYPE_INT8,
        DATA_TYPE_UINT8,
        DATA_TYPE_BOOLEAN_ARRAY,
        DATA_TYPE_INT8_ARRAY,
        DATA_TYPE_UINT8_ARRAY
    } data_type_t;
    typedef enum { B_FALSE, B_TRUE } boolean_t;

    typedef unsigned char uchar_t;
    typedef unsigned int uint_t;

    int nvlist_alloc(nvlist_t **, uint_t, int);
    void nvlist_free(nvlist_t *);

    int nvlist_unpack(char *, size_t, nvlist_t **, int);

    void dump_nvlist(nvlist_t *, int);
    int nvlist_dup(nvlist_t *, nvlist_t **, int);

    int nvlist_add_boolean(nvlist_t *, const char *);
    int nvlist_add_boolean_value(nvlist_t *, const char *, boolean_t);
    int nvlist_add_byte(nvlist_t *, const char *, uchar_t);
    int nvlist_add_int8(nvlist_t *, const char *, int8_t);
    int nvlist_add_uint8(nvlist_t *, const char *, uint8_t);
    int nvlist_add_int16(nvlist_t *, const char *, int16_t);
    int nvlist_add_uint16(nvlist_t *, const char *, uint16_t);
    int nvlist_add_int32(nvlist_t *, const char *, int32_t);
    int nvlist_add_uint32(nvlist_t *, const char *, uint32_t);
    int nvlist_add_int64(nvlist_t *, const char *, int64_t);
    int nvlist_add_uint64(nvlist_t *, const char *, uint64_t);
    int nvlist_add_string(nvlist_t *, const char *, const char *);
    int nvlist_add_nvlist(nvlist_t *, const char *, nvlist_t *);
    int nvlist_add_boolean_array(nvlist_t *, const char *,
        const boolean_t *, uint_t);
    int nvlist_add_byte_array(nvlist_t *, const char *,
        const uchar_t *, uint_t);
    int nvlist_add_int8_array(nvlist_t *, const char *,
        const int8_t *, uint_t);
    int nvlist_add_uint8_array(nvlist_t *, const char *,
        const uint8_t *, uint_t);
    int nvlist_add_int16_array(nvlist_t *, const char *,
        const int16_t *, uint_t);
    int nvlist_add_uint16_array(nvlist_t *, const char *,
        const uint16_t *, uint_t);
    int nvlist_add_int32_array(nvlist_t *, const char *,
        const int32_t *, uint_t);
    int nvlist_add_uint32_array(nvlist_t *, const char *,
        const uint32_t *, uint_t);
    int nvlist_add_int64_array(nvlist_t *, const char *,
        const int64_t *, uint_t);
    int nvlist_add_uint64_array(nvlist_t *, const char *,
        const uint64_t *, uint_t);
    int nvlist_add_string_array(nvlist_t *, const char *,
        const char * const *, uint_t);
    int nvlist_add_nvlist_array(nvlist_t *, const char *,
        const nvlist_t * const *, uint_t);

    nvpair_t *nvlist_next_nvpair(nvlist_t *, nvpair_t *);
    nvpair_t *nvlist_prev_nvpair(nvlist_t *, nvpair_t *);
    char *nvpair_name(nvpair_t *);
    data_type_t nvpair_type(nvpair_t *);
    int nvpair_type_is_array(nvpair_t *);
    int nvpair_value_boolean_value(nvpair_t *, boolean_t *);
    int nvpair_value_byte(nvpair_t *, uchar_t *);
    int nvpair_value_int8(nvpair_t *, int8_t *);
    int nvpair_value_uint8(nvpair_t *, uint8_t *);
    int nvpair_value_int16(nvpair_t *, int16_t *);
    int nvpair_value_uint16(nvpair_t *, uint16_t *);
    int nvpair_value_int32(nvpair_t *, int32_t *);
    int nvpair_value_uint32(nvpair_t *, uint32_t *);
    int nvpair_value_int64(nvpair_t *, int64_t *);
    int nvpair_value_uint64(nvpair_t *, uint64_t *);
    int nvpair_value_string(nvpair_t *, char **);
    int nvpair_value_nvlist(nvpair_t *, nvlist_t **);
    int nvpair_value_boolean_array(nvpair_t *, boolean_t **, uint_t *);
    int nvpair_value_byte_array(nvpair_t *, uchar_t **, uint_t *);
    int nvpair_value_int8_array(nvpair_t *, int8_t **, uint_t *);
    int nvpair_value_uint8_array(nvpair_t *, uint8_t **, uint_t *);
    int nvpair_value_int16_array(nvpair_t *, int16_t **, uint_t *);
    int nvpair_value_uint16_array(nvpair_t *, uint16_t **, uint_t *);
    int nvpair_value_int32_array(nvpair_t *, int32_t **, uint_t *);
    int nvpair_value_uint32_array(nvpair_t *, uint32_t **, uint_t *);
    int nvpair_value_int64_array(nvpair_t *, int64_t **, uint_t *);
    int nvpair_value_uint64_array(nvpair_t *, uint64_t **, uint_t *);
    int nvpair_value_string_array(nvpair_t *, char ***, uint_t *);
    int nvpair_value_nvlist_array(nvpair_t *, nvlist_t ***, uint_t *);
"""

SOURCE = """
#include <libzfs/sys/nvpair.h>
"""

LIBRARY = "nvpair"

# vim: softtabstop=4 tabstop=4 expandtab shiftwidth=4
