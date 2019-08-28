#!/bin/ksh
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

#
# DESCRIPTION
# verify 'zfs snapshot <list of snapshots>' works correctly
#
# STRATEGY
# 1. Create multiple datasets
# 2. Create multiple snapshots with a list of valid and invalid
#    snapshot names
# 3. Verify the valid snapshot creation

. $STF_SUITE/include/libtest.shlib

ZFS_MAX_DATASET_NAME_LEN=256

function cleanup
{
	for ds in $datasets; do
		datasetexists $ds && log_must zfs destroy -r $ds
	done
	zfs destroy -r $TESTPOOL/TESTFS4
}
datasets="$TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS2
    $TESTPOOL/$TESTFS3"

# We subtract 3 for slash (/), at (@), and the terminating nul (\0)
SNAPSHOT_XXX=$(printf 'x%.0s' \
    {1..$(($ZFS_MAX_DATASET_NAME_LEN - ${#TESTPOOL} - ${#TESTFS1} - 3))})

invalid_args=("$TESTPOOL/$TESTFS1@now $TESTPOOL/$TESTFS2@now \
    $TESTPOOL/$TESTFS@blah?" "$TESTPOOL/$TESTFS1@blah* \
    $TESTPOOL/$TESTFS2@blah? $TESTPOOL/$TESTFS3@blah%" \
    "$TESTPOOL/$TESTFS1@x$SNAPSHOT_XXX $TESTPOOL/$TESTFS2@300 \
    $TESTPOOL/$TESTFS3@300")

valid_args=("$TESTPOOL/$TESTFS1@snap $TESTPOOL/$TESTFS2@snap \
    $TESTPOOL/$TESTFS3@snap" "$TESTPOOL/$TESTFS1@$SNAPSHOT_XXX \
    $TESTPOOL/$TESTFS2@2 $TESTPOOL/$TESTFS3@s")

log_assert "verify zfs supports multiple consistent snapshots"
log_onexit cleanup
typeset -i i=1
test_data=$STF_SUITE/tests/functional/cli_root/zpool_upgrade/blockfiles/*.bz2

log_note "destroy a list of valid snapshots"
for ds in $datasets; do
	log_must zfs create $ds
	log_must cp -r $test_data /$ds
done
i=0
while (( i < ${#valid_args[*]} )); do
	log_must zfs snapshot ${valid_args[i]}
	for token in ${valid_args[i]}; do
		log_must snapexists $token && \
		    log_must zfs destroy $token
	done
	((i = i + 1))
done
log_note "destroy a list of invalid snapshots"
i=0
while (( i < ${#invalid_args[*]} )); do
	log_mustnot zfs snapshot ${invalid_args[i]}
	for token in ${invalid_args[i]}; do
		log_mustnot snapexists $token
	done
	((i = i + 1))
done
log_note "verify multiple snapshot transaction group"
txg_group=$(zdb -Pd $TESTPOOL | grep snap | awk '{print $7}')
for i in 1 2 3; do
	txg_tag=$(echo "$txg_group" | nawk -v j=$i 'FNR == j {print}')
	[[ $txg_tag != $(echo "$txg_group" | \
	    nawk -v j=$i 'FNR == j {print}') ]] \
	    && log_fail "snapshots belong to different transaction groups"
done
log_note "verify snapshot contents"
for ds in $datasets; do
	diff -q -r /$ds /$ds/.zfs/snapshot/snap > /dev/null 2>&1
	if [[ $? -eq 1 ]]; then
		log_fail "snapshot contents are different from" \
		    "the filesystem"
	fi
done

# We subtract 3 + 7 + 7 + 1 = 18 for three slashes (/), strlen("TESTFSA") == 7,
# strlen("TESTFSB") == 7, and the terminating nul (\0)
DATASET_XXX=$(printf 'x%.0s' \
    {1..$(($ZFS_MAX_DATASET_NAME_LEN - ${#TESTPOOL} - ${#TESTFS3} - 18))})

log_note "verify multiple snapshot with -r option"
log_must zfs create $TESTPOOL/TESTFS4
log_must zfs create -p $TESTPOOL/$TESTFS3/TESTFSA$DATASET_XXX/TESTFSB
log_mustnot zfs snapshot -r $TESTPOOL/$TESTFS1@snap1 $TESTPOOL/$TESTFS2@snap1 \
        $TESTPOOL/$TESTFS3@snap1 $TESTPOOL/TESTFS4@snap1
log_must zfs rename $TESTPOOL/$TESTFS3/TESTFSA$DATASET_XXX \
    $TESTPOOL/$TESTFS3/TESTFSA
log_must zfs snapshot -r $TESTPOOL/$TESTFS1@snap1 $TESTPOOL/$TESTFS2@snap1 \
        $TESTPOOL/$TESTFS3@snap1 $TESTPOOL/TESTFS4@snap1

log_pass "zfs multiple snapshot verified correctly"
