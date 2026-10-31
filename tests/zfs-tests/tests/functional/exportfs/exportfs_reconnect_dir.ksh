#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# A directory opened by file handle after its dentry was dropped is
# reconnected to the dentry tree (exportfs reconnect_path(), as knfsd
# does), which asks zpl_get_name() for its name in its parent.  The
# reconnected path must be the directory's current path, in microzap and
# fat ZAP directories, after renames, and on a case-insensitive dataset.
#
# STRATEGY:
# 1. Create a small (microzap) and a large (fat ZAP) directory of
#    subdirectories on a case-sensitive and a case-insensitive dataset.
# 2. Take file handles of some subdirectories; rename some of them,
#    within their parent and to another parent, and remove one.
# 3. Drop the dentry cache, open each handle and check the path the
#    kernel reports for it (/proc/self/fd), or ESTALE for the removed one.
#

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL/ci && destroy_dataset $TESTPOOL/ci
	rm -rf $TESTDIR/small $TESTDIR/big $TESTDIR/other
}

log_assert "Directories opened by handle reconnect to their current path"
log_onexit cleanup

log_must zfs create -o casesensitivity=insensitive \
    -o mountpoint=$TESTDIR/ci $TESTPOOL/ci

typeset base
for base in $TESTDIR $TESTDIR/ci; do
	log_must mkdir $base/small $base/big $base/other
	log_must eval "mkdir $base/small/Dir{0..9}"
	log_must eval "mkdir $base/big/Dir{0..4999}"
done

log_must python3 - $TESTDIR $TESTDIR/ci <<'PY'
import ctypes, errno, os, sys

libc = ctypes.CDLL(None, use_errno=True)

class FH(ctypes.Structure):
    _fields_ = [("handle_bytes", ctypes.c_uint), ("handle_type", ctypes.c_int),
                ("f_handle", ctypes.c_ubyte * 128)]

def handle(path):
    fh = FH(handle_bytes=128)
    mnt = ctypes.c_int()
    if libc.name_to_handle_at(-100, path.encode(), ctypes.byref(fh),
                              ctypes.byref(mnt), 0) != 0:
        sys.exit("name_to_handle_at %s: %s" %
                 (path, os.strerror(ctypes.get_errno())))
    return fh

fails = 0
cases = []
for base in sys.argv[1:]:
    mfd = os.open(base, os.O_RDONLY | os.O_DIRECTORY)
    for d in ("small", "big"):
        n = 10 if d == "small" else 5000
        for i in (0, 1, 2, n // 2, n - 1):
            p = "%s/%s/Dir%d" % (base, d, i)
            cases.append([mfd, handle(p), p])
    # renamed within the parent, to another parent, and removed
    for d in ("small", "big"):
        old = "%s/%s/Dir1" % (base, d)
        new = "%s/%s/Renamed" % (base, d)
        os.rename(old, new)
        [c for c in cases if c[2] == old][0][2] = new
        old = "%s/%s/Dir2" % (base, d)
        new = "%s/other/Moved-%s" % (base, d)
        os.rename(old, new)
        [c for c in cases if c[2] == old][0][2] = new
    old = "%s/big/Dir0" % base
    os.rmdir(old)
    [c for c in cases if c[2] == old][0][2] = None

os.sync()
with open("/proc/sys/vm/drop_caches", "w") as f:
    f.write("2")

for mfd, fh, want in cases:
    fd = libc.open_by_handle_at(mfd, ctypes.byref(fh), os.O_RDONLY)
    if fd < 0:
        err = ctypes.get_errno()
        if want is None and err == errno.ESTALE:
            print("removed: ESTALE")
            continue
        print("FAIL: open_by_handle_at for %s: %s" % (want, os.strerror(err)))
        fails += 1
        continue
    got = os.readlink("/proc/self/fd/%d" % fd)
    os.close(fd)
    if got != want:
        print("FAIL: reconnected as %s, expected %s" % (got, want))
        fails += 1
    else:
        print("ok: %s" % got)
sys.exit(1 if fails else 0)
PY

log_pass "Directories opened by handle reconnect to their current path"
