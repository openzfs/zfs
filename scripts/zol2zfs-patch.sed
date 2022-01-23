#!/bin/sed -f

s:cmd:usr/src/cmd:g
s:include/libzfs.h:usr/src/lib/libzfs/common/libzfs.h:g
s:include/libzfs_core.h:usr/src/lib/libzfs_core/common/libzfs_core.h:g
s:include/sys:lib/libzpool/common/sys:g
s:include/sys:usr/src/uts/common/fs/zfs/sys:g
s:include/sys:usr/src/uts/common/sys:g
s:include/zfs_fletcher.h:usr/src/common/zfs/zfs_fletcher.h:g
s:include:usr/src/common/zfs:g
s:lib/libzfs:usr/src/lib/libzfs/common:g
s:lib/libzfs_core:usr/src/lib/libzfs_core/common:g
s:lib/libzpool:lib/libzpool/common:g
s:lib/libzpool:usr/src/lib/libzpool:g
s:man/man7/zpool-features.7:usr/src/man/man5/zpool-features.5:g
s:man/man8/zfs.8:usr/src/man/man1m/zfs.1m:g
s:module/nvpair:usr/src/common/nvpair:g
s:module/zcommon:usr/src/common/zfs/:g
s:module/zfs:usr/src/uts/common/fs/zfs:g
s:tests/zfs-tests:test/zfs-tests:g
