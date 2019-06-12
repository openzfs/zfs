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

from __future__ import absolute_import, division, print_function

import os
from enum import Enum

from .exceptions import *
from .models import VDevStat, VDevTree
from .util import *
from ._constants import ZPOOL_CONFIG, ZFS_USERSPACE_PROP
from ._nvlist import nvlist_in, nvlist_out, _nvlist_to_dict
from .bindings.libnvpair import lib as libnvpair
from .bindings.libzfs import lib as libzfs
from .bindings.libzfs import ffi as _ffi

ENCODING = "utf-8"
ZPOOL_ITERATOR_FUNC_TYPE = 'zpool_iter_f'
ZFS_ITERATOR_FUNC_TYPE = 'zfs_iter_f'


def _is_root():
    return os.geteuid() == 0


def _unsafe_unpack_nvlist(nvlist):
    return _nvlist_to_dict(nvlist, {})


def libzfs_init():
    """
    Returns a handle to libzfs
    :return: libzfs_handle
    """
    libzfs_handle = libzfs.libzfs_init()
    if libzfs_handle == _ffi.NULL:
        raise LibZfsInitError()
    return libzfs_handle


def libzfs_fini(libzfs_handle):
    """
    Closes the libzfs handle
    :param libzfs_handle:
    :return: void
    """
    libzfs.libzfs_fini(libzfs_handle)


def libzfs_errno(libzfs_handle):
    """
    Returns the error number for the issue currently plaguing the current libzfs connection
    :param libzfs_handle:
    :return: int errno
    """
    return libzfs.libzfs_errno(libzfs_handle)


def libzfs_error_description(libzfs_handle):
    """
    Returns a description of the most severe error affecting the ZFS artifact
    :param libzfs_handle:
    :return: str
    """
    error_desc = libzfs.libzfs_error_description(libzfs_handle)
    return _ffi.string(error_desc)


def zpool_open(libzfs_handle, pool_name):
    """
    Returns a handle for the requested pool
    Will not open a pool if it is in the FAULTED state
    :returns zpool_handle
    """
    zpool_handle = libzfs.zpool_open(libzfs_handle, pool_name)
    if zpool_handle == _ffi.NULL:
        raise ZpoolOpenError(pool_name.decode(ENCODING))
    return zpool_handle


def zpool_open_canfail(libzfs_handle, pool_name):
    """
    Returns a handle for the requested pool, even if the pool is currently
    in the FAULTED state
    :returns zpool_handle
    """
    zpool_handle = libzfs.zpool_open_canfail(libzfs_handle, pool_name)
    if zpool_handle == _ffi.NULL:
        raise ZpoolOpenError(pool_name.decode(ENCODING))
    return zpool_handle


def zpool_close(zpool_handle):
    """
    Closes a zpool handle
    :param zpool_handle:
    :return: void
    """
    libzfs.zpool_close(zpool_handle)


def zpool_get_name(zpool_handle):
    """
    Returns the canonical name for the given zpool handle
    :param zpool_handle:
    :return: zpool_name str
    """
    pool_name = libzfs.zpool_get_name(zpool_handle)
    return _ffi.string(pool_name)


def zpool_get_config(zpool_handle):
    """
    Returns the configuration for the given zpool
    :param zpool_handle:
    :return: dict: Current configuration of the Zpool
    """
    oldconfig = {}
    with nvlist_out(oldconfig) as props_nvlist:
        config = libzfs.zpool_get_config(zpool_handle, props_nvlist)
        return _unsafe_unpack_nvlist(config)


def zpool_get_features(zpool_handle):
    """
    Returns the features supported by the given zpool
    :param zpool_handle:
    :return: dict: Current features enabled in the Zpool
    """
    output = libzfs.zpool_get_features(zpool_handle)
    if output == _ffi.NULL:
        raise ZpoolConfigFeatureFetchError()
    return _unsafe_unpack_nvlist(output)


def zpool_get_state(zpool_handle):
    """
    Returns the current state of the given zpool
    :param zpool_handle:
    :return: int
    """
    return libzfs.zpool_get_state(zpool_handle)


def zpool_get_state_str(zpool_handle):
    """
    Returns the current state of the given pool as a string
    :param zpool_handle:
    :return: str
    """
    state = libzfs.zpool_get_state_str(zpool_handle)
    return _ffi.string(state)


def zpool_prop_to_name(zpool_prop):
    """
    Returns the canonical name for a property enum int
    :param zpool_prop:
    :return:
    """
    if isinstance(zpool_prop, Enum):
        zpool_prop = zpool_prop.value
    return libzfs.zpool_prop_to_name(zpool_prop)


def zpool_prop_values(zpool_prop):
    """
    Returns the property's accepted values
    :param zpool_prop:
    :return: str: All the accepted values
    """
    if isinstance(zpool_prop, Enum):
        zpool_prop = zpool_prop.value
    props = libzfs.zpool_prop_values(zpool_prop)
    return _ffi.string(props)


def zpool_get_prop(zpool_handle, zpool_prop, zprop_source, literal):
    """
    Return the value of a property, for a zpool, as a byte string array
    :param zpool_handle:
    :param zpool_prop:
    :param zprop_source:
    :param literal: If True, then numbers are left as exact values, else they're converted to a human-readable form
    :return: str: Value of property requested
    """
    buf = _ffi.new("char []", 1024)
    buflen = len(buf)
    if isinstance(zpool_prop, Enum):
        zpool_prop = zpool_prop.value
    if isinstance(zprop_source, Enum):
        zprop_source = zprop_source.value
    ret = libzfs.zpool_get_prop(zpool_handle, zpool_prop, buf, buflen, zprop_source, literal)
    if ret != 0:
        raise ZpoolPropertyFetchError(zpool_prop.decode(ENCODING))
    return _ffi.string(buf)


def zpool_get_status(zpool_handle, msgid=None, errata=None):
    """
    Returns status code mapping to zpool_status_t
    Only returns the most severe status of all conditions affecting a pool
    :param zpool_handle: Zpool Handle
    :param msgid:
    :param errata:
    :returns status zpool_status_t: Status enum value
    :returns zfs_msg_id str: Message id corresponding to zfs_msgid_table[] in libzfs/libzfs_status.c
    """
    zfs_msg_id = None
    if msgid is None:
        c_msgid = _ffi.new("char **")
    else:
        c_msgid = msgid
    if errata is None:
        c_errata = _ffi.NULL
    else:
        c_errata = errata
    status = libzfs.zpool_get_status(zpool_handle, c_msgid, c_errata)
    if c_msgid[0] and c_msgid != _ffi.NULL:
        zfs_msg_id = _ffi.string(c_msgid[0])
    return status, zfs_msg_id


def zpool_get_errlog(zpool_handle):
    """
    See function with the same name in libzfs_pool.c
    :param zpool_handle:
    :return: int
    """
    with nvlist_out({}) as props_nvlist:
        output = libzfs.zpool_get_errlog(zpool_handle, props_nvlist)
        if output != 0:
            raise MemoryError()
        return _unsafe_unpack_nvlist(props_nvlist[0])


def zpool_get_history(zpool_handle):
    """
    Returns a list of all commands run on a zpool.
    Can fail due to permission issues.
    :param zpool_handle:
    :return: dict zpool_history
    """
    if not _is_root():
        raise InsufficientPerms("<zpool_get_history>")
    with nvlist_out({}) as cmd_history_nvlist:
        output = libzfs.zpool_get_history(zpool_handle, cmd_history_nvlist)
        if output != 0:
            raise ZpoolHistoryFetchError()
        return _unsafe_unpack_nvlist(cmd_history_nvlist[0])


# BEGIN Dataset related functions
def zfs_open(libzfs_handle, pool_name, type_mask):
    """
    Returns a handle to a ZFS dataset
    :param libzfs_handle:
    :param pool_name:
    :param type_mask: of type zfs_type_t
    :return: zfs_handle_t
    """
    if isinstance(type_mask, Enum):
        type_mask = type_mask.value
    zfs_handle = libzfs.zfs_open(libzfs_handle, pool_name, type_mask)
    if zfs_handle == _ffi.NULL:
        raise ZfsDatasetOpenError(pool_name.decode(ENCODING))
    return zfs_handle


def zfs_close(zfs_handle):
    """
    Closes the handle for a ZFS dataset
    :param zfs_handle:
    :return: void
    """
    libzfs.zfs_close(zfs_handle)


def zfs_get_type(zfs_handle):
    """
    Returns the type of the given ZFS dataset
    :param zfs_handle:
    :return: zfs_type_t
    """
    return libzfs.zfs_get_type(zfs_handle)


def zfs_get_name(zfs_handle):
    """
    Returns the canonical name of the given ZFS dataset
    :param zfs_handle:
    :return: str
    """
    ds_name = libzfs.zfs_get_name(zfs_handle)
    return _ffi.string(ds_name)


def zfs_get_pool_handle(zfs_handle):
    """
    Returns the zpool handle the ZFS dataset resides in
    :param zfs_handle:
    :return: zpool_handle
    """
    return libzfs.zfs_get_pool_handle(zfs_handle)


def zfs_get_pool_name(zfs_handle):
    """
    Returns the canonical name for the zpool the ZFS dataset resides in
    :param zfs_handle:
    :return: str
    """
    pool_name = libzfs.zfs_get_pool_name(zfs_handle)
    return _ffi.string(pool_name)


def zfs_prop_get(zfs_handle, zfs_prop, zprop_source, literal):
    """
    Returns the value for a particular property of a ZFS dataset
    :param zfs_handle: dataset handle
    :param zfs_prop:
    :param zprop_source:
    :param literal: If True, then numbers are left as exact values, else they're converted to a human-readable form
    :returns prop_buf str:
    :returns stat_buf str:
    """
    prop_buf = _ffi.new("char []", 1024)
    prop_buf_len = len(prop_buf)
    stat_buf = _ffi.new("char []", 1024)
    stat_buf_len = len(stat_buf)
    if isinstance(zfs_prop, Enum):
        zfs_prop = zfs_prop.value
    if isinstance(zprop_source, Enum):
        zprop_source = zprop_source.value
    ret = libzfs.zfs_prop_get(zfs_handle, zfs_prop, prop_buf, prop_buf_len, zprop_source, stat_buf, stat_buf_len,
                              literal)
    if ret != 0:
        raise ZfsPropertyFetchError(zfs_prop.decode(ENCODING))
    return (_ffi.string(prop_buf), _ffi.string(stat_buf))


def zfs_prop_get_userquota(zfs_handle, user_name, prefix, literal):
    """
    Get a userspace property for a particular user
    :param zfs_handle:
    :param user_name:
    :param prefix: The userspace property we want
    :param literal: If True, then numbers are left as exact values, else they're converted to a human-readable form
    :return: str
    """
    prop_buf = _ffi.new("char []", 1024)
    prop_buf_len = len(prop_buf)
    if isinstance(prefix, Enum):
        prefix = prefix.value
    prop_name = prefix + user_name
    ret = libzfs.zfs_prop_get_userquota(zfs_handle, prop_name, prop_buf, prop_buf_len, literal)
    if ret != 0:
        raise ZfsPropertyFetchError(prop_name.decode(ENCODING))
    return _ffi.string(prop_buf)


def zfs_get_all_props(zfs_handle):
    """
    Returns all the properties set for a ZFS dataset
    :param zfs_handle:
    :return: dict
    """
    props_nvlist = libzfs.zfs_get_all_props(zfs_handle)
    return _unsafe_unpack_nvlist(props_nvlist)


def zfs_get_user_props(zfs_handle):
    """
    Returns all the userspace properties for a ZFS dataset
    :param zfs_handle:
    :return: dict
    """
    props_nvlist = libzfs.zfs_get_user_props(zfs_handle)
    return _unsafe_unpack_nvlist(props_nvlist)


def zfs_get_fsacl(zfs_handle):
    acl = {}
    print(_unsafe_unpack_nvlist(zfs_handle.zfs_user_props))
    with nvlist_out(acl) as nvlist_acl:
        out = libzfs.zfs_get_fsacl(zfs_handle, nvlist_acl)
        print(out)
        return _unsafe_unpack_nvlist(nvlist_acl[0])


# END Dataset related functions

def get_zfs_userspace_users(zfs_handle):
    """
    Returns all the uids + gids that have interacted with the dataset
    Can fail due to permission issues
    :param zfs_handle:
    :return int[]: Array of all the uids + gids
    """
    if not _is_root():
        raise InsufficientPerms("<get_zfs_userspace_users>")

    users = []

    @_ffi.callback('zfs_userspace_cb_t')
    def py_zfs_userspace_callback(arg, domain, rid, space):
        users.append(rid)
        return 0

    # Don't really care what prop we use, as we will get all the values anyway
    zfs_userquota_prop = ZFS_USERSPACE_PROP.ZFS_PROP_USERUSED.value

    out = libzfs.zfs_userspace(zfs_handle, zfs_userquota_prop, py_zfs_userspace_callback, _ffi.NULL)
    if out != 0:
        raise ZfsDatasetUserspaceError(ZFS_USERSPACE_PROP(zfs_userquota_prop))
    return users


def get_zpool_names(libzfs_handle):
    pool_names = []

    @_ffi.callback(ZPOOL_ITERATOR_FUNC_TYPE)
    def py_zpool_iter(pool_handle, data=None):
        if pool_handle.zpool_name != _ffi.NULL:
            pool_names.append(_ffi.string(pool_handle.zpool_name))
        return 0

    out = libzfs.zpool_iter(libzfs_handle, py_zpool_iter, _ffi.NULL)
    if out != 0:
        raise ZpoolIterError()
    return [coerce_to_compatible(name) for name in pool_names]


def get_zfs_dataset_names(libzfs_handle):
    root_dataset_handles = []
    dataset_names = []

    @_ffi.callback(ZFS_ITERATOR_FUNC_TYPE)
    def py_zfs_iter_root(zfs_handle, data=None):
        root_dataset_handles.append(zfs_handle)
        return 0

    @_ffi.callback(ZFS_ITERATOR_FUNC_TYPE)
    def py_zfs_iter_children(zfs_handle, data=None):
        if zfs_handle.zfs_name != _ffi.NULL:
            dataset_names.append(_ffi.string(zfs_handle.zfs_name))
        return 0

    out = libzfs.zfs_iter_root(libzfs_handle, py_zfs_iter_root, _ffi.NULL)
    if out != 0:
        raise ZfsDatasetIterError()
    for root_ds in root_dataset_handles:
        out = libzfs.zfs_iter_filesystems(root_ds, py_zfs_iter_children, _ffi.NULL)
        if out != 0:
            raise ZfsDatasetIterError()
    return [coerce_to_compatible(name) for name in dataset_names]


def construct_vdev_tree(zpool_handle):
    vdev_tree = {}
    config = libzfs.zpool_get_config(zpool_handle, _ffi.NULL)
    if config == _ffi.NULL:
        raise ZpoolConfigFeatureFetchError("Failed to get config")
    with nvlist_out({}) as vdev_nvlist:
        ret = libnvpair.nvlist_lookup_nvlist(config, ZPOOL_CONFIG.ZPOOL_CONFIG_VDEV_TREE.value, vdev_nvlist)
        if ret != 0:
            raise ZpoolConfigFeatureFetchError("Failed to get VDev list")
        # vdev_nvlist is of type `nvlist_t **`
        # In order to access nvlist*, we need to deref the head of the nvlist**
        vdev_tree = stringify_dict(_unsafe_unpack_nvlist(vdev_nvlist[0]))
        vdev_tree = VDevTree.construct_from_vdev_tree(vdev_tree)
    return vdev_tree
