#
# Copyright 2019 Hudson River Trading LLC.
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


def stringify_dict(d):
    ret = {}
    if isinstance(d, dict):
        for k, v in d.items():
            if isinstance(v, dict):
                v = stringify_dict(v)
            if isinstance(v, list):
                v = [stringify_dict(vd) for vd in v]
            if isinstance(k, bytes):
                k = k.decode("utf-8")
            if isinstance(v, bytes):
                v = v.decode("utf-8")
            ret[k] = v
    else:
        return d
    return ret


def coerce_to_compatible(obj, encoding="utf-8"):
    ret = obj
    if isinstance(ret, (int, float)):
        ret = str(obj)
    if isinstance(ret, dict):
        d = ret
        for k, v in d.items():
            d[k] = coerce_to_compatible(v)
        ret = d
    if isinstance(ret, (bytes, bytearray)):
        ret = str(ret, encoding=encoding)
    if isinstance(ret, list):
        coerced_elems = []
        for elem in ret:
            elem = str(coerce_to_compatible(elem))
            coerced_elems.append(elem)
        ret = str(coerced_elems)
    if ret is None:
        ret = ""
    return ret
