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

# DESCRIPTION:
#	zfs rewrite -N records the directory entry cookie of files and
#	directories that have none or a stale one, such as objects linked
#	by an implementation that didn't record cookies, without rewriting
#	their data.
#
# STRATEGY:
#	1. With zfs_parent_cookie=0, create a directory tree with a fat ZAP
#	   directory, files with data, a hard link and renamed directories,
#	   so that no object has a valid cookie.
#	2. Save the files' checksums and block pointers.
#	3. Run zfs rewrite -N -r on the top of the tree, and zfs rewrite -N
#	   without -r on another directory.
#	4. Check with zdb that every object, including the directories given
#	   on the command line, has a valid cookie.
#	5. Check that the files' checksums and block pointers are unchanged.
#	6. On Linux, open directories by file handle after a remount and check
#	   that the name of each one and of each ancestor is found by its
#	   cookie, without searching a directory (dirent_name kstat).
#	7. Check that -N is refused with options that rewrite data.
#	8. Check that -N needs write permission on the object: an
#	   unprivileged user can record the cookie of their own directory,
#	   not of one they can't write.

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

typeset tmp=$TEST_BASE_DIR/zfs_rewrite_parent_cookie
typeset user=zrwnuser
typeset group=zrwngroup

function cleanup
{
	restore_tunable PARENT_COOKIE
	rm -rf $TESTDIR/top $TESTDIR/solo $TESTDIR/theirs $TESTDIR/mine $tmp.*
	del_user $user
	del_group $group
}

# Check that each path's object has the given cookie state, or doesn't.
function check_cookies # [!]state path...
{
	typeset want=$1
	typeset p obj state
	shift
	log_must sync_pool $TESTPOOL
	for p in "$@"; do
		obj=$(get_objnum $TESTDIR/$p)
		state=$(zdb -ddddd $TESTPOOL/$TESTFS $obj |
		    awk '/^\tcookie\t/ { print $3 }')
		[[ -n $state ]] || state=absent
		if [[ $want == !* ]]; then
			[[ $state != ${want#!} ]] ||
			    log_fail "$p ($obj): cookie $state"
		else
			[[ $state == $want ]] ||
			    log_fail "$p ($obj): cookie $state, expected $want"
		fi
		log_note "$p ($obj): cookie $state"
	done
}

log_assert "zfs rewrite -N records directory entry cookies, not data"
log_onexit cleanup
log_must save_tunable PARENT_COOKIE

set -A objs top top/small top/big top/big/d0 top/big/d1500 top/big/d2999 \
    top/big/d7 top/big/d7/sub top/small/moved top/big/renamed \
    top/small/f0 top/small/f1 top/small/f2 top/big/link solo

# Link everything without recording cookies.
log_must set_tunable32 PARENT_COOKIE 0
log_must mkdir $TESTDIR/top $TESTDIR/top/small $TESTDIR/top/big $TESTDIR/solo
log_must eval "mkdir $TESTDIR/top/big/d{0..2999}"
log_must mkdir $TESTDIR/top/big/d7/sub
log_must mv $TESTDIR/top/big/d8 $TESTDIR/top/small/moved
log_must mv $TESTDIR/top/big/d9 $TESTDIR/top/big/renamed
for f in f0 f1 f2; do
	log_must dd if=/dev/urandom of=$TESTDIR/top/small/$f bs=128k count=4
done
log_must ln $TESTDIR/top/small/f2 $TESTDIR/top/big/link
log_must set_tunable32 PARENT_COOKIE 1
check_cookies !valid ${objs[@]}

log_must sync_pool $TESTPOOL
for f in f0 f1 f2; do
	xxh128digest $TESTDIR/top/small/$f > $tmp.$f.hash
	log_must eval "zdb -Ovv $TESTPOOL/$TESTFS top/small/$f |
	    awk '/ L0 / { print \$3 }' > $tmp.$f.bps"
done

log_must zfs rewrite -N -r $TESTDIR/top
log_must zfs rewrite -N $TESTDIR/solo
check_cookies valid ${objs[@]}

log_must sync_pool $TESTPOOL
for f in f0 f1 f2; do
	log_must [ "$(xxh128digest $TESTDIR/top/small/$f)" = \
	    "$(cat $tmp.$f.hash)" ]
	log_must eval "zdb -Ovv $TESTPOOL/$TESTFS top/small/$f |
	    awk '/ L0 / { print \$3 }' > $tmp.$f.bps2"
	log_must [ -s $tmp.$f.bps ]
	log_must cmp $tmp.$f.bps $tmp.$f.bps2
done

if is_linux; then
	log_must python3 - $TESTDIR $TESTPOOL/$TESTFS top/big/d7/sub \
	    top/small/moved top/big/renamed top/big/d1500 solo <<'PY'
import ctypes, os, subprocess, sys

libc = ctypes.CDLL(None, use_errno=True)
mnt, ds, rels = sys.argv[1], sys.argv[2], sys.argv[3:]

class FH(ctypes.Structure):
    _fields_ = [("handle_bytes", ctypes.c_uint), ("handle_type", ctypes.c_int),
                ("f_handle", ctypes.c_ubyte * 128)]

def handle(p):
    fh, mid = FH(handle_bytes=128), ctypes.c_int()
    if libc.name_to_handle_at(-100, p.encode(), ctypes.byref(fh),
                              ctypes.byref(mid), 0) != 0:
        sys.exit("name_to_handle_at %s: %s" %
                 (p, os.strerror(ctypes.get_errno())))
    return fh

def kstats():
    lines = open("/proc/spl/kstat/zfs/dirent_name").read().splitlines()
    return {w[0]: int(w[2]) for w in (l.split() for l in lines[2:])}

fails = 0
for rel, fh in [(r, handle(mnt + "/" + r)) for r in rels]:
    for c in ("unmount", "mount"):
        subprocess.run(["zfs", c, ds], check=True)
    mfd = os.open(mnt, os.O_RDONLY | os.O_DIRECTORY)
    k0 = kstats()
    fd = libc.open_by_handle_at(mfd, ctypes.byref(fh), os.O_RDONLY)
    k1 = kstats()
    os.close(mfd)
    if fd < 0:
        print("FAIL: open_by_handle_at %s: %s" %
              (rel, os.strerror(ctypes.get_errno())))
        fails += 1
        continue
    got = os.readlink("/proc/self/fd/%d" % fd)
    os.close(fd)
    hits = k1["cookie_hits"] - k0["cookie_hits"]
    searches = k1["searches"] - k0["searches"]
    # the directory and each ancestor below the root, by cookie
    want = len(rel.split("/"))
    msg = "%s: %s, cookie_hits +%d, searches +%d" % (rel, got, hits,
                                                      searches)
    if got != mnt + "/" + rel or hits != want or searches != 0:
        print("FAIL: %s, expected cookie_hits +%d, searches +0" %
              (msg, want))
        fails += 1
    else:
        print("ok: " + msg)
sys.exit(1 if fails else 0)
PY
fi

for opt in -C -P -S -W "-o 0" "-l 131072"; do
	log_mustnot zfs rewrite -N $opt $TESTDIR/top/small/f0
done

# Write permission on the object is needed, not a writable descriptor.
log_must add_group $group
log_must add_user $group $user
log_must set_tunable32 PARENT_COOKIE 0
log_must mkdir -m 0755 $TESTDIR/theirs $TESTDIR/mine
log_must chown $user $TESTDIR/mine
log_must set_tunable32 PARENT_COOKIE 1
log_mustnot user_run $user zfs rewrite -N $TESTDIR/theirs
log_must user_run $user zfs rewrite -N $TESTDIR/mine
check_cookies !valid theirs
check_cookies valid mine

log_pass "zfs rewrite -N records directory entry cookies, not data"
