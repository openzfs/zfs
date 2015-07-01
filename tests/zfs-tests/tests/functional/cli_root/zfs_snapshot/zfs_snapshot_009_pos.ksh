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
# Copyright (c) 2012 by Delphix. All rights reserved.
#

#
# DESCRIPTION
# verify 'zfs snapshot <list of snapshots>' works correctly
#
# STRATEGY
# 1. Create multiple datasets
# 2. Create mutiple snapshots with a list of valid and invalid
#    snapshot names
# 3. Verify the valid snpashot creation

. $STF_SUITE/include/libtest.shlib

function cleanup
{
	for ds in $datasets; do
		datasetexists $ds && log_must $ZFS destroy -r $ds
	done
	$ZFS destroy -r $TESTPOOL/TESTFS4
}
datasets="$TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS2
    $TESTPOOL/$TESTFS3"

invalid_args=("$TESTPOOL/$TESTFS1@now $TESTPOOL/$TESTFS2@now \
    $TESTPOOL/$TESTFS@blah?" "$TESTPOOL/$TESTFS1@blah* \
    $TESTPOOL/$TESTFS2@blah? $TESTPOOL/$TESTFS3@blah%" \
    "$TESTPOOL/$TESTFS1@$($PYTHON -c 'print "x" * 300') $TESTPOOL/$TESTFS2@300 \
    $TESTPOOL/$TESTFS3@300")

valid_args=("$TESTPOOL/$TESTFS1@snap $TESTPOOL/$TESTFS2@snap \
    $TESTPOOL/$TESTFS3@snap" "$TESTPOOL/$TESTFS1@$($PYTHON -c 'print "x" * 200')\
    $TESTPOOL/$TESTFS2@2 $TESTPOOL/$TESTFS3@s")

log_assert "verify zfs supports multiple consistent snapshots"
log_onexit cleanup
typeset -i i=1
test_data=$STF_SUITE/tests/functional/cli_root/zpool_upgrade/*.bz2

log_note "destroy a list of valid snapshots"
for ds in $datasets; do
	log_must $ZFS create $ds
	log_must $CP -r $test_data /$ds
done
i=0
while (( i < ${#valid_args[*]} )); do
	log_must $ZFS snapshot ${valid_args[i]}
	for token in ${valid_args[i]}; do
		log_must snapexists $token && \
		    log_must $ZFS destroy $token
	done
	((i = i + 1))
done
log_note "destroy a list of invalid snapshots"
i=0
while (( i < ${#invalid_args[*]} )); do
	log_mustnot $ZFS snapshot ${invalid_args[i]}
	for token in ${invalid_args[i]}; do
		log_mustnot snapexists $token
	done
	((i = i + 1))
done
log_note "verify multiple snapshot transaction group"
txg_group=$($ZDB -Pd $TESTPOOL | $GREP snap | $AWK '{print $7}')
for i in 1 2 3; do
	txg_tag=$($ECHO "$txg_group" | $NAWK -v j=$i 'FNR == j {print}')
	[[ $txg_tag != $($ECHO "$txg_group" | \
	    $NAWK -v j=$i 'FNR == j {print}') ]] \
	    && log_fail "snapshots belong to differnt transaction groups"
done
log_note "verify snapshot contents"
for ds in $datasets; do
	status=$($DIRCMP /$ds /$ds/.zfs/snapshot/snap | $GREP "different")
	[[ -z $status ]] || log_fail "snapshot contents are different from" \
	    "the filesystem"
done

log_note "verify multiple snapshot with -r option"
log_must $ZFS create $TESTPOOL/TESTFS4
log_must $ZFS create -p $TESTPOOL/$TESTFS3/TESTFSA$($PYTHON -c 'print "x" * 210')/TESTFSB
log_mustnot $ZFS snapshot -r $TESTPOOL/$TESTFS1@snap1 $TESTPOOL/$TESTFS2@snap1 \
        $TESTPOOL/$TESTFS3@snap1 $TESTPOOL/TESTFS4@snap1
log_must $ZFS rename  $TESTPOOL/$TESTFS3/TESTFSA$($PYTHON -c 'print "x" * 210') \
    $TESTPOOL/$TESTFS3/TESTFSA
log_must $ZFS snapshot -r $TESTPOOL/$TESTFS1@snap1 $TESTPOOL/$TESTFS2@snap1 \
        $TESTPOOL/$TESTFS3@snap1 $TESTPOOL/TESTFS4@snap1

log_pass "zfs multiple snapshot verified correctly"
