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
# A file that knfsd has to reconnect (an export with subtree_check, after
# the server dropped the file's dentry) is reconnected by name: the kernel
# asks zpl_get_name() for the file's name in its parent, in a buffer of
# NAME_MAX + 1 bytes.  With longname=on, a file can have a link whose name
# doesn't fit that buffer and another, shorter one in the same directory.
# zpl_get_name() must return the name that fits, whichever entry its
# cookie or the directory's order would give first, so that the NFS
# client's open file doesn't go stale (ESTALE).
#
# STRATEGY:
# 1. On a longname=on dataset, in a microzap and in a fat ZAP directory,
#    give a file a short name, then a hard link with a 300-byte name that
#    comes first in the directory's order (the order a search visits
#    entries in) and, linked last, is the one its cookie names.
# 2. Export the dataset over NFSv3 on loopback with subtree_check and
#    mount it.
# 3. On the client, open each file by its short name and read it; wait for
#    nfsd's file cache to let the files go, drop the server's dentry and
#    inode caches, and read the open files again: each read must succeed
#    (not ESTALE).
#

verify_runnable "global"

typeset fs=$TESTPOOL/longname mnt=$TESTDIR/longname
typeset nfsmnt=$TEST_BASE_DIR/exportfs_reconnect_file_longname.nfs
typeset exported=

function cleanup
{
	typeset n

	umount -f $nfsmnt 2>/dev/null
	[[ -d $nfsmnt ]] && rmdir $nfsmnt
	if [[ -n $exported ]]; then
		log_must exportfs -u 127.0.0.1:$mnt
		# nfsd's export cache keeps references to the file system,
		# even after exportfs -u and -f, until nfsd stops: restart it,
		# with as many threads as it had.
		n=$(cat /proc/fs/nfsd/threads)
		log_must rpc.nfsd 0
		log_must rpc.nfsd $n
	fi
	datasetexists $fs && destroy_dataset $fs -r
}

log_assert "A file reconnected by knfsd is found by a name that fits"
log_onexit cleanup

check_nfs

log_must zfs create -o longname=on -o mountpoint=$mnt $fs
typeset pad=$(printf 'L%.0s' {1..290})
typeset d first long i
for d in small big; do
	log_must mkdir $mnt/$d
	[[ $d == big ]] && log_must eval "touch $mnt/$d/pad{0..4999}"
	log_must eval "echo $d >$mnt/$d/short"
	# The long link must come first in the directory's order.
	long=
	for i in {1..200}; do
		log_must ln $mnt/$d/short $mnt/$d/$i-$pad
		# readdir order (os.listdir), not ls, which may sort
		first=$(python3 -c 'import os, re, sys
print(next(n for n in os.listdir(sys.argv[1])
    if re.fullmatch(r"short|[0-9]+-L+", n)))' $mnt/$d)
		[[ $first == $i-$pad ]] && { long=$i-$pad; break; }
		log_must rm $mnt/$d/$i-$pad
	done
	[[ -n $long ]] || log_fail "$d: no long name came first in 200 tries"
done

log_must exportfs -o rw,no_root_squash,subtree_check,fsid=4242 \
    127.0.0.1:$mnt
exported=1
log_must mkdir $nfsmnt
log_must mount -t nfs -o vers=3,nolock,actimeo=0,lookupcache=none \
    127.0.0.1:$mnt $nfsmnt

log_must python3 - $nfsmnt/small/short $nfsmnt/big/short <<'PY'
import os, sys, time

fds = {}
for p in sys.argv[1:]:
    fds[p] = os.open(p, os.O_RDONLY)
    print("opened %s: %r" % (p, os.pread(fds[p], 16, 0)))
# nfsd keeps the files it read open for a few seconds (its file cache),
# which keeps their dentries; then drop the server's caches
time.sleep(12)
for i in range(2):
    with open("/proc/sys/vm/drop_caches", "w") as f:
        f.write("2")
    time.sleep(1)
fails = 0
for p, fd in fds.items():
    try:
        os.fstat(fd)
        print("ok: %s: read %r after the reconnect" % (p, os.pread(fd, 16, 0)))
    except OSError as e:
        print("FAIL: %s: %s" % (p, e))
        fails += 1
sys.exit(1 if fails else 0)
PY

log_pass "A file reconnected by knfsd is found by a name that fits"
