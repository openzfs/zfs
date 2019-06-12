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

"""
Python wrappers for **libzfs** library.
"""
from __future__ import absolute_import, division, print_function

from ._constants import (
    ZPOOL_PROP,
    ZPOOL_STATUS,
    ZPOOL_ERRATA,
    ZPROP_SOURCE,
    ZFS_TYPE,
    ZFS_PROP,
    ZFS_USERSPACE_PROP,
    ZFS_USERSPACE_PROP_PREFIX,
    ZPOOL_CONFIG,
    SCAN_STATE,
    VDEV_STATE,
    VDEV_AUX_STATE
)

from ._libzfs import (
    libzfs_init,
    libzfs_fini,
    libzfs_errno,
    libzfs_error_description,
    zpool_open,
    zpool_open_canfail,
    zpool_close,
    zpool_get_name,
    zpool_get_config,
    zpool_get_features,
    zpool_get_state,
    zpool_get_state_str,
    zpool_prop_to_name,
    zpool_prop_values,
    zpool_get_prop,
    zpool_get_status,
    zpool_get_errlog,
    zpool_get_history,
    zfs_open,
    zfs_close,
    zfs_get_type,
    zfs_get_name,
    zfs_get_pool_handle,
    zfs_get_pool_name,
    zfs_prop_get,
    zfs_prop_get_userquota,
    zfs_get_all_props,
    zfs_get_user_props,
    zfs_get_fsacl,
    get_zfs_userspace_users,
    get_zpool_names,
    get_zfs_dataset_names,
    construct_vdev_tree,
)

from .util import (
    stringify_dict,
    coerce_to_compatible
)

from .models import (
    VDevStat,
    VDevTree,
    PoolScanStat,
)

__all__ = [
    'ZPOOL_PROP',
    'ZPOOL_STATUS',
    'ZPOOL_ERRATA',
    'ZPROP_SOURCE',
    'ZFS_TYPE',
    'ZFS_PROP',
    'ZFS_USERSPACE_PROP',
    'ZFS_USERSPACE_PROP_PREFIX',
    'ZPOOL_CONFIG',
    'SCAN_STATE',
    'VDEV_STATE',
    'VDEV_AUX_STATE',
    'stringify_dict',
    'coerce_to_compatible',
    'VDevTree',
    'VDevStat',
    'PoolScanStat',
    'libzfs_init',
    'libzfs_fini',
    'libzfs_errno',
    'libzfs_error_description',
    'zpool_open',
    'zpool_open_canfail',
    'zpool_close',
    'zpool_get_name',
    'zpool_get_config',
    'zpool_get_features',
    'zpool_get_state',
    'zpool_get_state_str',
    'zpool_prop_to_name',
    'zpool_prop_values',
    'zpool_get_prop',
    'zpool_get_status',
    'zpool_get_errlog',
    'zpool_get_history',
    'zfs_open',
    'zfs_close',
    'zfs_get_type',
    'zfs_get_name',
    'zfs_get_pool_handle',
    'zfs_get_pool_name',
    'zfs_prop_get',
    'zfs_prop_get_userquota',
    'zfs_get_all_props',
    'zfs_get_user_props',
    'zfs_get_fsacl',
    'get_zfs_userspace_users',
    'get_zpool_names',
    'get_zfs_dataset_names',
    'construct_vdev_tree',
]

# vim: softtabstop=4 tabstop=4 expandtab shiftwidth=4
