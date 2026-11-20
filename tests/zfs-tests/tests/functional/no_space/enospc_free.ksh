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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/no_space/enospc.cfg

#
# DESCRIPTION:
# On a full pool, operations that free space succeed: truncate, hole
# punching, rm, and zfs destroy, also after administrative operations
# have used the part of the slop space they may use.
#
# STRATEGY:
# 1. Write files, one of them with 4K records into a snapshot, and fill
#    the pool until ENOSPC.
# 2. Truncate a file and punch a hole in another one.
# 3. Fill the pool again and remove a file; its object must not be left
#    in the unlinked set.
# 4. Fill the pool again and set user properties until ENOSPC.
# 5. Remove the file held by the snapshot, which frees nothing, then
#    remove and truncate files that are not in the snapshot.
# 6. Destroy the snapshot and a second file system.
#

verify_runnable "global"

log_onexit default_cleanup_noexit

typeset -i fills=0

function fill_pool
{
	typeset -i i ret

	for i in 1 2 3; do
		file_write -o create -f $TESTDIR/fill.$fills.$i -b $BLOCKSZ \
		    -c $NUM_WRITES -d $DATA
		ret=$?
		(( ret != ENOSPC )) && \
		    log_fail "fill.$fills.$i returned $ret rather than ENOSPC"
		sync_pool $TESTPOOL true
	done
	(( fills++ ))
}

function mkfile_mb # file MiB [recsize]
{
	log_must file_write -o create -f $1 -b ${3:-1048576} \
	    -c $(( $2 * 1048576 / ${3:-1048576} )) -d $DATA
}

function unlinked_count
{
	typeset -i links=$(kstat_dataset $TESTPOOL/$TESTFS nunlinks)
	typeset -i unlinked=$(kstat_dataset $TESTPOOL/$TESTFS nunlinked)

	echo $(( links - unlinked ))
}

# Removed objects are freed asynchronously; wait for the unlinked set.
function unlinked_drained
{
	typeset -i i

	for i in $(seq 30); do
		(( $(unlinked_count) == 0 )) && return 0
		sleep 1
	done
	log_note "$(unlinked_count) objects left in the unlinked set"
	return 1
}

log_assert "Space can be freed on a full pool"

default_setup_noexit $DISK_LARGE
log_must zfs set compression=off $TESTPOOL/$TESTFS
log_must zfs create -o compression=off $TESTPOOL/$TESTFS2
mkfile_mb /$TESTPOOL/$TESTFS2/file 16

log_must zfs set recordsize=4k $TESTPOOL/$TESTFS
mkfile_mb $TESTDIR/snap.file 128 4096
log_must zfs inherit recordsize $TESTPOOL/$TESTFS
log_must zfs snapshot $TESTPOOL/$TESTFS@snap
for f in trunc punch rm keep keep2; do
	mkfile_mb $TESTDIR/$f.file 16
done
sync_pool $TESTPOOL true

fill_pool
log_must truncate -s 0 $TESTDIR/trunc.file
log_must test ! -s $TESTDIR/trunc.file
if is_linux && command -v fallocate > /dev/null; then
	log_must fallocate -p -o 1M -l 8M $TESTDIR/punch.file
fi

fill_pool
log_must test $(unlinked_count) -eq 0
log_must rm $TESTDIR/rm.file
log_must unlinked_drained

fill_pool
val=$(printf "%7999s" "" | tr " " x)
typeset -i n=0
while (( n < 1000 )); do
	props=""
	for i in $(seq 16); do
		props="$props user:p$n.$i=$val"
	done
	zfs set $props $TESTPOOL/$TESTFS > /dev/null 2>&1 || break
	(( n++ ))
done
(( n == 1000 )) && log_fail "user properties never ran out of space"
while (( n < 20000 )); do
	zfs set user:p$n=$val $TESTPOOL/$TESTFS > /dev/null 2>&1 || break
	(( n++ ))
done
(( n == 20000 )) && log_fail "user properties never ran out of space"
log_mustnot zfs set user:last=$val $TESTPOOL/$TESTFS
sync_pool $TESTPOOL true

log_must rm $TESTDIR/snap.file
sync_pool $TESTPOOL true
log_must rm $TESTDIR/keep.file
log_must truncate -s 0 $TESTDIR/keep2.file
log_must unlinked_drained

log_must zfs destroy $TESTPOOL/$TESTFS@snap
log_must zfs destroy $TESTPOOL/$TESTFS2
log_must test "$(zpool list -Ho health $TESTPOOL)" = "ONLINE"

log_pass "Space can be freed on a full pool"
