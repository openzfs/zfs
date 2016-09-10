#!/bin/sed -f

s:usr/src/uts/common/fs/zfs/sys:include/sys:g
s:usr/src/uts/common/fs/zfs:module/zfs:g
s:usr/src/lib/libzpool:lib/libzpool:g
s:usr/src/cmd:cmd:g
s:usr/src/common/nvpair:module/nvpair:g
s:usr/src/lib/libzfs/common/libzfs.h:include/libzfs.h:g
s:usr/src/man/man1m/zfs.1m:man/man8/zfs.8:g
s:usr/src/uts/common/sys:include/sys:g
s:usr/src/lib/libzfs_core/common/libzfs_core.h:include/libzfs_core.h:g
s:usr/src/lib/libzfs/common:lib/libzfs:g
s:usr/src/lib/libzfs_core/common:lib/libzfs_core:g
s:lib/libzpool/common/sys:include/sys:g
s:lib/libzpool/common:lib/libzpool:g

s:usr/src/test/zfs-tests/include:tests/zfs-tests/include:g
s:usr/src/test/zfs-tests/runfiles:tests/runfiles:g
s:usr/src/test/zfs-tests/tests/functional:tests/zfs-tests/tests/functional:g
s:usr/src/test/zfs-tests/tests/perf:tests/zfs-tests/tests/perf:g
s:usr/src/test/test-runner/cmd/run.py:tests/test-runner/cmd/test-runner.py:g
