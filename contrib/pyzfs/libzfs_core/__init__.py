# Copyright 2015 ClusterHQ. See LICENSE file for details.
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
please visit its `GitHub repository <https://github.com/ClusterHQ/pyzfs>`_.

.. data:: MAXNAMELEN

    Maximum length of any ZFS name.
'''

from ._constants import (
    MAXNAMELEN,
)

from ._libzfs_core import (
    lzc_create,
    lzc_clone,
    lzc_rollback,
    lzc_rollback_to,
    lzc_snapshot,
    lzc_snap,
    lzc_destroy_snaps,
    lzc_bookmark,
    lzc_get_bookmarks,
    lzc_destroy_bookmarks,
    lzc_snaprange_space,
    lzc_hold,
    lzc_release,
    lzc_get_holds,
    lzc_send,
    lzc_send_space,
    lzc_receive,
    lzc_receive_with_header,
    lzc_recv,
    lzc_exists,
    is_supported,
    lzc_promote,
    lzc_rename,
    lzc_destroy,
    lzc_inherit_prop,
    lzc_set_prop,
    lzc_get_props,
    lzc_list_children,
    lzc_list_snaps,
    receive_header,
)

__all__ = [
    'ctypes',
    'exceptions',
    'MAXNAMELEN',
    'lzc_create',
    'lzc_clone',
    'lzc_rollback',
    'lzc_rollback_to',
    'lzc_snapshot',
    'lzc_snap',
    'lzc_destroy_snaps',
    'lzc_bookmark',
    'lzc_get_bookmarks',
    'lzc_destroy_bookmarks',
    'lzc_snaprange_space',
    'lzc_hold',
    'lzc_release',
    'lzc_get_holds',
    'lzc_send',
    'lzc_send_space',
    'lzc_receive',
    'lzc_receive_with_header',
    'lzc_recv',
    'lzc_exists',
    'is_supported',
    'lzc_promote',
    'lzc_rename',
    'lzc_destroy',
    'lzc_inherit_prop',
    'lzc_set_prop',
    'lzc_get_props',
    'lzc_list_children',
    'lzc_list_snaps',
    'receive_header',
]

# vim: softtabstop=4 tabstop=4 expandtab shiftwidth=4
