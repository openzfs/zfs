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
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy.cfg

#
# DESCRIPTION:
# 'zfs destroy -t <type>' destroys the target when its type is one of
# the listed types.
#
# STRATEGY:
# 1. Create a file system, a volume, snapshots, a clone and a bookmark.
# 2. Destroy each of them with -t naming its type, by name, by alias
#    and in a list of types.
# 3. Verify that -n with a matching type still destroys nothing.
# 4. Verify that -r and -R still destroy dependents of other types.
#

verify_runnable "both"

fs=$TESTPOOL/$TESTFS1
vol=$TESTPOOL/$TESTVOL

function cleanup
{
	datasetexists $fs && destroy_dataset $fs -Rf
	datasetexists $vol && destroy_dataset $vol -Rf
	datasetexists $TESTPOOL/clone && destroy_dataset $TESTPOOL/clone -Rf
}

log_assert "'zfs destroy -t' destroys datasets of the listed types"
log_onexit cleanup

# File systems and volumes, by name, alias and in a list.
for t in filesystem fs volume,filesystem all; do
	log_must zfs create $fs
	log_must zfs destroy -t $t $fs
	log_must datasetnonexists $fs
done

if is_global_zone; then
	for t in volume vol fs,vol,bookmark; do
		log_must zfs create -V $VOLSIZE $vol
		log_must zfs destroy -t $t $vol
		log_must datasetnonexists $vol
	done
fi

# Snapshots, including ranges and lists of snapshots.
log_must zfs create $fs
for t in snapshot snap all; do
	log_must zfs snapshot $fs@a
	log_must zfs destroy -t $t $fs@a
	log_must datasetnonexists $fs@a
done

log_must zfs snapshot $fs@a
log_must zfs snapshot $fs@b
log_must zfs snapshot $fs@c
log_must zfs destroy -t snap $fs@a,c
log_must datasetnonexists $fs@a
log_must datasetnonexists $fs@c
log_must datasetexists $fs@b
log_must zfs destroy $fs@b
log_must zfs snapshot $fs@a
log_must zfs snapshot $fs@b
log_must zfs snapshot $fs@c
log_must zfs destroy -t snap $fs@a%c
log_must datasetnonexists $fs@a
log_must datasetnonexists $fs@b
log_must datasetnonexists $fs@c

# Deferred destruction of a snapshot.
log_must zfs snapshot $fs@a
log_must zfs destroy -d -t snap $fs@a
log_must datasetnonexists $fs@a

# Bookmarks.
for t in bookmark snap,bookmark; do
	log_must zfs snapshot $fs@a
	log_must zfs bookmark $fs@a $fs#bm
	log_must zfs destroy -t $t $fs#bm
	log_mustnot bkmarkexists $fs#bm
	log_must zfs destroy $fs@a
done

# A dry run with a matching type changes nothing and succeeds.
log_must zfs snapshot $fs@a
log_must zfs destroy -n -t snapshot $fs@a
log_must datasetexists $fs@a
log_must zfs destroy $fs@a
log_must zfs destroy -n -t fs $fs
log_must datasetexists $fs

# With -r, only the target is checked: dependents of other types go too.
log_must zfs create $fs/child
log_must zfs snapshot -r $fs@a
if is_global_zone; then
	log_must zfs create -V $VOLSIZE $fs/vol
fi
log_must zfs destroy -r -t filesystem $fs
log_must datasetnonexists $fs
log_must datasetnonexists $fs/child
log_must datasetnonexists $fs/vol

# Likewise with -R, which also destroys clones of the snapshot.
log_must zfs create $fs
log_must zfs snapshot $fs@a
log_must zfs clone $fs@a $TESTPOOL/clone
log_must zfs destroy -R -t snapshot $fs@a
log_must datasetnonexists $fs@a
log_must datasetnonexists $TESTPOOL/clone
log_must datasetexists $fs
log_must zfs destroy -t fs $fs
log_must datasetnonexists $fs

log_pass "'zfs destroy -t' destroys datasets of the listed types"
