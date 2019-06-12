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
Tests for _nvlist module.
The tests convert from a `dict` to C ``nvlist_t`` and back to a `dict`
and verify that no information is lost and value types are correct.
The tests also check that various error conditions like unsupported
value types or out of bounds values are detected.
"""
from __future__ import absolute_import, division, print_function

import unittest

from .._nvlist import nvlist_in, nvlist_out, _lib
from ..ctypes import (
    uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t,
    uint64_t, int64_t, boolean_t, uchar_t
)


class TestNVList(unittest.TestCase):

    def _dict_to_nvlist_to_dict(self, props):
        res = {}
        nv_in = nvlist_in(props)
        with nvlist_out(res) as nv_out:
            _lib.nvlist_dup(nv_in, nv_out, 0)
        return res

    def _assertIntDictsEqual(self, dict1, dict2):
        self.assertEqual(
            len(dict1), len(dict1),
            b"resulting dictionary is of different size")
        for key in dict1.keys():
            self.assertEqual(int(dict1[key]), int(dict2[key]))

    def _assertIntArrayDictsEqual(self, dict1, dict2):
        self.assertEqual(
            len(dict1), len(dict1),
            b"resulting dictionary is of different size")
        for key in dict1.keys():
            val1 = dict1[key]
            val2 = dict2[key]
            self.assertEqual(
                len(val1), len(val2), b"array values of different sizes")
            for x, y in zip(val1, val2):
                self.assertEqual(int(x), int(y))

    def test_empty(self):
        res = self._dict_to_nvlist_to_dict({})
        self.assertEqual(len(res), 0, b"expected empty dict")

    def test_invalid_key_type(self):
        with self.assertRaises(TypeError):
            self._dict_to_nvlist_to_dict({1: None})

    def test_invalid_val_type__tuple(self):
        with self.assertRaises(TypeError):
            self._dict_to_nvlist_to_dict({b"key": (1, 2)})

    def test_invalid_val_type__set(self):
        with self.assertRaises(TypeError):
            self._dict_to_nvlist_to_dict({b"key": set(1, 2)})

    def test_invalid_array_val_type(self):
        with self.assertRaises(TypeError):
            self._dict_to_nvlist_to_dict({b"key": [(1, 2), (3, 4)]})

    def test_invalid_array_of_arrays_val_type(self):
        with self.assertRaises(TypeError):
            self._dict_to_nvlist_to_dict({b"key": [[1, 2], [3, 4]]})

    def test_string_value(self):
        props = {b"key": b"value"}
        res = self._dict_to_nvlist_to_dict(props)
        self.assertEqual(props, res)

    def test_implicit_boolean_value(self):
        props = {b"key": None}
        res = self._dict_to_nvlist_to_dict(props)
        self.assertEqual(props, res)

    def test_boolean_values(self):
        props = {b"key1": True, b"key2": False}
        res = self._dict_to_nvlist_to_dict(props)
        self.assertEqual(props, res)

    def test_explicit_boolean_true_value(self):
        props = {b"key": boolean_t(1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_boolean_false_value(self):
        props = {b"key": boolean_t(0)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_boolean_invalid_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": boolean_t(2)}
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_boolean_another_invalid_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": boolean_t(-1)}
            self._dict_to_nvlist_to_dict(props)

    def test_uint64_value(self):
        props = {b"key": 1}
        res = self._dict_to_nvlist_to_dict(props)
        self.assertEqual(props, res)

    def test_uint64_max_value(self):
        props = {b"key": 2 ** 64 - 1}
        res = self._dict_to_nvlist_to_dict(props)
        self.assertEqual(props, res)

    def test_uint64_too_large_value(self):
        props = {b"key": 2 ** 64}
        with self.assertRaises(OverflowError):
            self._dict_to_nvlist_to_dict(props)

    def test_uint64_negative_value(self):
        props = {b"key": -1}
        with self.assertRaises(OverflowError):
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_uint64_value(self):
        props = {b"key": uint64_t(1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_uint64_max_value(self):
        props = {b"key": uint64_t(2 ** 64 - 1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_uint64_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": uint64_t(2 ** 64)}
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_uint64_negative_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": uint64_t(-1)}
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_uint32_value(self):
        props = {b"key": uint32_t(1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_uint32_max_value(self):
        props = {b"key": uint32_t(2 ** 32 - 1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_uint32_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": uint32_t(2 ** 32)}
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_uint32_negative_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": uint32_t(-1)}
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_uint16_value(self):
        props = {b"key": uint16_t(1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_uint16_max_value(self):
        props = {b"key": uint16_t(2 ** 16 - 1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_uint16_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": uint16_t(2 ** 16)}
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_uint16_negative_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": uint16_t(-1)}
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_uint8_value(self):
        props = {b"key": uint8_t(1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_uint8_max_value(self):
        props = {b"key": uint8_t(2 ** 8 - 1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_uint8_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": uint8_t(2 ** 8)}
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_uint8_negative_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": uint8_t(-1)}
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_byte_value(self):
        props = {b"key": uchar_t(1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_byte_max_value(self):
        props = {b"key": uchar_t(2 ** 8 - 1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_byte_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": uchar_t(2 ** 8)}
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_byte_negative_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": uchar_t(-1)}
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_int64_value(self):
        props = {b"key": int64_t(1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_int64_max_value(self):
        props = {b"key": int64_t(2 ** 63 - 1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_int64_min_value(self):
        props = {b"key": int64_t(-(2 ** 63))}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_int64_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": int64_t(2 ** 63)}
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_int64_too_small_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": int64_t(-(2 ** 63) - 1)}
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_int32_value(self):
        props = {b"key": int32_t(1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_int32_max_value(self):
        props = {b"key": int32_t(2 ** 31 - 1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_int32_min_value(self):
        props = {b"key": int32_t(-(2 ** 31))}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_int32_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": int32_t(2 ** 31)}
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_int32_too_small_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": int32_t(-(2 ** 31) - 1)}
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_int16_value(self):
        props = {b"key": int16_t(1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_int16_max_value(self):
        props = {b"key": int16_t(2 ** 15 - 1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_int16_min_value(self):
        props = {b"key": int16_t(-(2 ** 15))}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_int16_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": int16_t(2 ** 15)}
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_int16_too_small_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": int16_t(-(2 ** 15) - 1)}
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_int8_value(self):
        props = {b"key": int8_t(1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_int8_max_value(self):
        props = {b"key": int8_t(2 ** 7 - 1)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_int8_min_value(self):
        props = {b"key": int8_t(-(2 ** 7))}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_explicit_int8_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": int8_t(2 ** 7)}
            self._dict_to_nvlist_to_dict(props)

    def test_explicit_int8_too_small_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": int8_t(-(2 ** 7) - 1)}
            self._dict_to_nvlist_to_dict(props)

    def test_nested_dict(self):
        props = {b"key": {}}
        res = self._dict_to_nvlist_to_dict(props)
        self.assertEqual(props, res)

    def test_nested_nested_dict(self):
        props = {b"key": {b"key": {}}}
        res = self._dict_to_nvlist_to_dict(props)
        self.assertEqual(props, res)

    def test_mismatching_values_array(self):
        props = {b"key": [1, b"string"]}
        with self.assertRaises(TypeError):
            self._dict_to_nvlist_to_dict(props)

    def test_mismatching_values_array2(self):
        props = {b"key": [True, 10]}
        with self.assertRaises(TypeError):
            self._dict_to_nvlist_to_dict(props)

    def test_mismatching_values_array3(self):
        props = {b"key": [1, False]}
        with self.assertRaises(TypeError):
            self._dict_to_nvlist_to_dict(props)

    def test_string_array(self):
        props = {b"key": [b"value", b"value2"]}
        res = self._dict_to_nvlist_to_dict(props)
        self.assertEqual(props, res)

    def test_boolean_array(self):
        props = {b"key": [True, False]}
        res = self._dict_to_nvlist_to_dict(props)
        self.assertEqual(props, res)

    def test_explicit_boolean_array(self):
        props = {b"key": [boolean_t(False), boolean_t(True)]}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntArrayDictsEqual(props, res)

    def test_uint64_array(self):
        props = {b"key": [0, 1, 2 ** 64 - 1]}
        res = self._dict_to_nvlist_to_dict(props)
        self.assertEqual(props, res)

    def test_uint64_array_too_large_value(self):
        props = {b"key": [0, 2 ** 64]}
        with self.assertRaises(OverflowError):
            self._dict_to_nvlist_to_dict(props)

    def test_uint64_array_negative_value(self):
        props = {b"key": [0, -1]}
        with self.assertRaises(OverflowError):
            self._dict_to_nvlist_to_dict(props)

    def test_mixed_explict_int_array(self):
        with self.assertRaises(TypeError):
            props = {b"key": [uint64_t(0), uint32_t(0)]}
            self._dict_to_nvlist_to_dict(props)

    def test_explict_uint64_array(self):
        props = {b"key": [uint64_t(0), uint64_t(1), uint64_t(2 ** 64 - 1)]}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntArrayDictsEqual(props, res)

    def test_explict_uint64_array_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": [uint64_t(0), uint64_t(2 ** 64)]}
            self._dict_to_nvlist_to_dict(props)

    def test_explict_uint64_array_negative_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": [uint64_t(0), uint64_t(-1)]}
            self._dict_to_nvlist_to_dict(props)

    def test_explict_uint32_array(self):
        props = {b"key": [uint32_t(0), uint32_t(1), uint32_t(2 ** 32 - 1)]}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntArrayDictsEqual(props, res)

    def test_explict_uint32_array_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": [uint32_t(0), uint32_t(2 ** 32)]}
            self._dict_to_nvlist_to_dict(props)

    def test_explict_uint32_array_negative_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": [uint32_t(0), uint32_t(-1)]}
            self._dict_to_nvlist_to_dict(props)

    def test_explict_uint16_array(self):
        props = {b"key": [uint16_t(0), uint16_t(1), uint16_t(2 ** 16 - 1)]}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntArrayDictsEqual(props, res)

    def test_explict_uint16_array_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": [uint16_t(0), uint16_t(2 ** 16)]}
            self._dict_to_nvlist_to_dict(props)

    def test_explict_uint16_array_negative_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": [uint16_t(0), uint16_t(-1)]}
            self._dict_to_nvlist_to_dict(props)

    def test_explict_uint8_array(self):
        props = {b"key": [uint8_t(0), uint8_t(1), uint8_t(2 ** 8 - 1)]}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntArrayDictsEqual(props, res)

    def test_explict_uint8_array_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": [uint8_t(0), uint8_t(2 ** 8)]}
            self._dict_to_nvlist_to_dict(props)

    def test_explict_uint8_array_negative_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": [uint8_t(0), uint8_t(-1)]}
            self._dict_to_nvlist_to_dict(props)

    def test_explict_byte_array(self):
        props = {b"key": [uchar_t(0), uchar_t(1), uchar_t(2 ** 8 - 1)]}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntArrayDictsEqual(props, res)

    def test_explict_byte_array_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": [uchar_t(0), uchar_t(2 ** 8)]}
            self._dict_to_nvlist_to_dict(props)

    def test_explict_byte_array_negative_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": [uchar_t(0), uchar_t(-1)]}
            self._dict_to_nvlist_to_dict(props)

    def test_explict_int64_array(self):
        props = {b"key": [
            int64_t(0), int64_t(1), int64_t(2 ** 63 - 1), int64_t(-(2 ** 63))]}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntArrayDictsEqual(props, res)

    def test_explict_int64_array_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": [int64_t(0), int64_t(2 ** 63)]}
            self._dict_to_nvlist_to_dict(props)

    def test_explict_int64_array_too_small_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": [int64_t(0), int64_t(-(2 ** 63) - 1)]}
            self._dict_to_nvlist_to_dict(props)

    def test_explict_int32_array(self):
        props = {b"key": [
            int32_t(0), int32_t(1), int32_t(2 ** 31 - 1), int32_t(-(2 ** 31))]}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntArrayDictsEqual(props, res)

    def test_explict_int32_array_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": [int32_t(0), int32_t(2 ** 31)]}
            self._dict_to_nvlist_to_dict(props)

    def test_explict_int32_array_too_small_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": [int32_t(0), int32_t(-(2 ** 31) - 1)]}
            self._dict_to_nvlist_to_dict(props)

    def test_explict_int16_array(self):
        props = {b"key": [
            int16_t(0), int16_t(1), int16_t(2 ** 15 - 1), int16_t(-(2 ** 15))]}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntArrayDictsEqual(props, res)

    def test_explict_int16_array_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": [int16_t(0), int16_t(2 ** 15)]}
            self._dict_to_nvlist_to_dict(props)

    def test_explict_int16_array_too_small_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": [int16_t(0), int16_t(-(2 ** 15) - 1)]}
            self._dict_to_nvlist_to_dict(props)

    def test_explict_int8_array(self):
        props = {b"key": [
            int8_t(0), int8_t(1), int8_t(2 ** 7 - 1), int8_t(-(2 ** 7))]}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntArrayDictsEqual(props, res)

    def test_explict_int8_array_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": [int8_t(0), int8_t(2 ** 7)]}
            self._dict_to_nvlist_to_dict(props)

    def test_explict_int8_array_too_small_value(self):
        with self.assertRaises(OverflowError):
            props = {b"key": [int8_t(0), int8_t(-(2 ** 7) - 1)]}
            self._dict_to_nvlist_to_dict(props)

    def test_dict_array(self):
        props = {b"key": [{b"key": 1}, {b"key": None}, {b"key": {}}]}
        res = self._dict_to_nvlist_to_dict(props)
        self.assertEqual(props, res)

    def test_implicit_uint32_value(self):
        props = {b"rewind-request": 1}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_implicit_uint32_max_value(self):
        props = {b"rewind-request": 2 ** 32 - 1}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_implicit_uint32_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"rewind-request": 2 ** 32}
            self._dict_to_nvlist_to_dict(props)

    def test_implicit_uint32_negative_value(self):
        with self.assertRaises(OverflowError):
            props = {b"rewind-request": -1}
            self._dict_to_nvlist_to_dict(props)

    def test_implicit_int32_value(self):
        props = {b"pool_context": 1}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_implicit_int32_max_value(self):
        props = {b"pool_context": 2 ** 31 - 1}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_implicit_int32_min_value(self):
        props = {b"pool_context": -(2 ** 31)}
        res = self._dict_to_nvlist_to_dict(props)
        self._assertIntDictsEqual(props, res)

    def test_implicit_int32_too_large_value(self):
        with self.assertRaises(OverflowError):
            props = {b"pool_context": 2 ** 31}
            self._dict_to_nvlist_to_dict(props)

    def test_implicit_int32_too_small_value(self):
        with self.assertRaises(OverflowError):
            props = {b"pool_context": -(2 ** 31) - 1}
            self._dict_to_nvlist_to_dict(props)

    def test_complex_dict(self):
        props = {
            b"key1": b"str",
            b"key2": 10,
            b"key3": {
                b"skey1": True,
                b"skey2": None,
                b"skey3": [
                    True,
                    False,
                    True
                ]
            },
            b"key4": [
                b"ab",
                b"bc"
            ],
            b"key5": [
                2 ** 64 - 1,
                1,
                2,
                3
            ],
            b"key6": [
                {
                    b"skey71": b"a",
                    b"skey72": b"b",
                },
                {
                    b"skey71": b"c",
                    b"skey72": b"d",
                },
                {
                    b"skey71": b"e",
                    b"skey72": b"f",
                }

            ],
            b"type": 2 ** 32 - 1,
            b"pool_context": -(2 ** 31)
        }
        res = self._dict_to_nvlist_to_dict(props)
        self.assertEqual(props, res)


# vim: softtabstop=4 tabstop=4 expandtab shiftwidth=4
