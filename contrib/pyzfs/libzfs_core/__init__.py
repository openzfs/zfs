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

'''
Python wrappers for **libzfs_core** library.

*libzfs_core* is intended to be a stable, committed interface for programmatic
administration of ZFS.
This wrapper provides one-to-one wrappers for libzfs_core API functions,
but the signatures and types are more natural to Python.
nvlists are wrapped as dictionaries or lists depending on their usage.
Some parameters have default values depending on typical use for
increased convenience.
Output parameters are not used and return values are directly returned.
Enumerations and bit flags become strings and lists of strings in Python.
Errors are reported as exceptions rather than integer errno-style
error codes.  The wrapper takes care to provide one-to-many mapping
of the error codes to the exceptions by interpreting a context
in which the error code is produced.

To submit an issue or contribute to development of this package
please visit its `GitHub repository <https://github.com/openzfs/zfs>`_.

.. data:: MAXNAMELEN

    Maximum length of any ZFS name.
'''
from __future__ import absolute_import, division, print_function

from ._constants import (
    MAXNAMELEN,
    ZCP_DEFAULT_INSTRLIMIT,
    ZCP_DEFAULT_MEMLIMIT,
    WRAPPING_KEY_LEN,
    zfs_key_location,
    zfs_keyformat,
    zio_encrypt
)

from ._libzfs_core import (
    lzc_bookmark,
    lzc_change_key,
    lzc_channel_program,
    lzc_channel_program_nosync,
    lzc_clone,
    lzc_create,
    lzc_destroy_bookmarks,
    lzc_destroy_snaps,
    lzc_exists,
    lzc_get_bookmarks,
    lzc_get_holds,
    lzc_hold,
    lzc_load_key,
    lzc_pool_checkpoint,
    lzc_pool_checkpoint_discard,
    lzc_promote,
    lzc_receive,
    lzc_receive_one,
    lzc_receive_resumable,
    lzc_receive_with_cmdprops,
    lzc_receive_with_header,
    lzc_receive_with_heal,
    lzc_release,
    lzc_reopen,
    lzc_rollback,
    lzc_rollback_to,
    lzc_send,
    lzc_send_resume,
    lzc_send_space,
    lzc_snaprange_space,
    lzc_snapshot,
    lzc_sync,
    lzc_unload_key,
    is_supported,
    lzc_recv,
    lzc_snap,
    lzc_rename,
    lzc_destroy,
    lzc_inherit_prop,
    lzc_get_props,
    lzc_set_props,
    lzc_list_children,
    lzc_list_snaps,
    receive_header,
)

__all__ = [
    'ctypes',
    'exceptions',
    'MAXNAMELEN',
    'ZCP_DEFAULT_INSTRLIMIT',
    'ZCP_DEFAULT_MEMLIMIT',
    'WRAPPING_KEY_LEN',
    'zfs_key_location',
    'zfs_keyformat',
    'zio_encrypt',
    'lzc_bookmark',
    'lzc_change_key',
    'lzc_channel_program',
    'lzc_channel_program_nosync',
    'lzc_clone',
    'lzc_create',
    'lzc_destroy_bookmarks',
    'lzc_destroy_snaps',
    'lzc_exists',
    'lzc_get_bookmarks',
    'lzc_get_holds',
    'lzc_hold',
    'lzc_load_key',
    'lzc_pool_checkpoint',
    'lzc_pool_checkpoint_discard',
    'lzc_promote',
    'lzc_receive',
    'lzc_receive_one',
    'lzc_receive_resumable',
    'lzc_receive_with_cmdprops',
    'lzc_receive_with_header',
    'lzc_receive_with_heal',
    'lzc_release',
    'lzc_reopen',
    'lzc_rollback',
    'lzc_rollback_to',
    'lzc_send',
    'lzc_send_resume',
    'lzc_send_space',
    'lzc_snaprange_space',
    'lzc_snapshot',
    'lzc_sync',
    'lzc_unload_key',
    'is_supported',
    'lzc_recv',
    'lzc_snap',
    'lzc_rename',
    'lzc_destroy',
    'lzc_inherit_prop',
    'lzc_get_props',
    'lzc_set_props',
    'lzc_list_children',
    'lzc_list_snaps',
    'receive_header',
]

# vim: softtabstop=4 tabstop=4 expandtab shiftwidth=4
