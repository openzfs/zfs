#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A pool can be imported from a cachefile written before a parity
#	epoch was added to its raidz vdev, once blocks are born in that
#	epoch.  Storing a table changes the labels and the MOS config in
#	its txg, and the cachefile only later, so a crash in between leaves
#	such a cachefile.
#
# STRATEGY:
#	1. Create a raidz2 pool of 512-byte sectors with a cachefile, write
#	   a file, copy the cachefile, and export the pool.
#	2. Add an epoch of parity 3 with zhack, which waits for it to start.
#	   The labels now have a table, the copy has none.
#	3. Import from the copy, which must not need the fallback to a
#	   device scan that zpool import -c takes when the cachefile cannot
#	   import the pool, and must take the layout from the labels.
#	4. Compare the file, check that the cachefile it imported into has
#	   the table, scrub, and verify that the pool has no errors.
#

verify_runnable "global"

typeset -r BASEDIR=$TEST_BASE_DIR/raidz_pe_cachefile.$$
typeset -r POOL=${TESTPOOL}_pecf
typeset -r CACHE=$BASEDIR/cachefile
typeset -r STALE=$BASEDIR/cachefile.stale
typeset -r MSG="spa_load($POOL, config untrusted): vdev 0: using the RAID-Z \
layout of its label"
typeset -i WIDTH=6
typeset -a VDEVS

original_dbgmsg_enable=$(get_tunable DBGMSG_ENABLE)

function cleanup
{
	poolexists $POOL && destroy_pool $POOL
	rm -rf $BASEDIR
	set_tunable32 DBGMSG_ENABLE $original_dbgmsg_enable
}

# count_entries <zdb arguments>: set n to the table entries zdb prints.
function count_entries
{
	typeset -i v

	v=$(zdb "$@" 2>/dev/null | grep -c 'raidz_parity_epochs\[')
	((n = v / 3))
}

log_onexit cleanup
log_assert "a cachefile older than a parity epoch still imports the pool"

log_must set_tunable32 DBGMSG_ENABLE 1
log_must mkdir -p $BASEDIR
for ((i = 0; i < WIDTH; i++)); do
	VDEVS[$i]=$BASEDIR/vdev$i
	log_must truncate -s 128M ${VDEVS[$i]}
done

log_must zpool create -f -o ashift=9 -o cachefile=$CACHE $POOL \
    raidz2 ${VDEVS[@]}
log_must dd if=/dev/urandom of=/$POOL/payload bs=1M count=8
log_must cp /$POOL/payload $BASEDIR/payload.ref
log_must cp $CACHE $STALE
log_must zpool export $POOL

# The blocks born from the start of the epoch, the MOS among them, have a
# third parity column.
log_must zhack -d $BASEDIR raidz_epochs $POOL 0 0:$WIDTH:2 next:$WIDTH:3
count_entries -l ${VDEVS[0]}
((n >= 2)) || log_fail "the labels show $n table entries"
count_entries -C -U $STALE
((n == 0)) || log_fail "the copy of the cachefile has a table"

log_must eval "zpool import -c $STALE -o cachefile=$CACHE $POOL \
    > $BASEDIR/import.out 2>&1"
cat $BASEDIR/import.out
log_mustnot grep -q "cachefile import failed" $BASEDIR/import.out
log_must eval "kstat dbgmsg | grep -qF '$MSG'"

log_must cmp /$POOL/payload $BASEDIR/payload.ref
count_entries -C -U $CACHE
((n >= 2)) || log_fail "the cachefile shows $n table entries"
log_must zpool scrub -w $POOL
log_must check_pool_status $POOL "errors" "No known data errors"
log_must check_pool_status $POOL "scan" "with 0 errors"

log_pass "a cachefile older than a parity epoch still imports the pool"
