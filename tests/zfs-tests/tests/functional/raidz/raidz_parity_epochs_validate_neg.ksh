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

#
# DESCRIPTION:
#	A RAIDZ parity-epoch table must fit the vdev it is stored on, and
#	zhack must refuse one that does not, or a vdev it cannot use, without
#	changing the pool.
#
# STRATEGY:
#	1. Create a three-wide RAIDZ1 pool holding a file and export it.
#	2. Require zhack to fail promptly, say why and leave the pool
#	   untouched, on a vdev that does not exist, a vdev id that is not a
#	   number, a triplet that is not one and a table that does not fit.
#	3. For each table that does not fit the vdev, on a pool of its own,
#	   require zhack to name the rule it breaks, write it with zhack -f,
#	   which skips that check, and require the import to fail with a
#	   diagnostic that names the rule.  The rescue tunable then allows a
#	   read-only import only, which keeps the data.
#	4. Require a table that fits, with a parity change at a txg the pool
#	   never reaches, to import, scrub clean and keep the data.
#	5. Require the table to travel under the org.openzfs label key.
#

verify_runnable "global"

typeset pool=${TESTPOOL}_pe_val
typeset workdir=$TEST_BASE_DIR/raidz_pe_val.$$
typeset -a disks
typeset -i far

function cleanup
{
	set_tunable32 RAIDZ_IGNORE_PARITY_EPOCHS 0
	poolexists $pool && destroy_pool $pool
	rm -rf $workdir
}

# A fresh three-wide RAIDZ1 pool holding the reference data, exported.
function make_pool
{
	poolexists $pool && destroy_pool $pool
	log_must zpool create -f $pool raidz1 ${disks[@]}
	log_must cp $workdir/data.ref /$pool/data
	log_must zpool export $pool
}

function check_refused # <description> <rule> <start:width:parity>...
{
	typeset desc=$1 rule=$2
	shift 2

	log_note "Table with $desc: $*"
	make_pool
	log_mustnot_expect "not valid for this vdev: $rule" \
	    zhack -d $workdir raidz_epochs $pool 0 "$@"
	log_must zhack -d $workdir raidz_epochs -f $pool 0 "$@"
	log_mustnot zpool import -d $workdir $pool
	log_mustnot eval "zdb -e -p $workdir -G $pool > $workdir/zdb.out 2>&1"
	log_must grep -qF "rejecting parity epoch table [error=22]: $rule" \
	    $workdir/zdb.out

	# A damaged table can be read around, read-only, but not repaired.
	log_must set_tunable32 RAIDZ_IGNORE_PARITY_EPOCHS 1
	log_mustnot zpool import -d $workdir $pool
	log_must zpool import -o readonly=on -d $workdir $pool
	log_must cmp /$pool/data $workdir/data.ref
	log_must_busy zpool export $pool
	log_must set_tunable32 RAIDZ_IGNORE_PARITY_EPOCHS 0
}

log_assert "A parity-epoch table must fit the vdev it is stored on"
log_onexit cleanup

log_must mkdir -p $workdir
for i in 0 1 2; do
	disks[i]=$workdir/vdev$i
	log_must truncate -s 256M ${disks[i]}
done
log_must dd if=/dev/urandom of=$workdir/data.ref bs=1M count=8
make_pool

# Far beyond any txg the test reaches, so no existing or new block uses it.
log_must eval "zdb -lu ${disks[0]} > $workdir/labels.out"
far=$(awk '/txg = /{print $3}' $workdir/labels.out | sort -n | tail -1)
log_must test $far -gt 0
(( far = far + 1000 ))

# zhack must fail, not hang, say why, and leave the pool as it was.
log_mustnot_expect "no such top-level vdev" \
    timeout 60 zhack -d $workdir raidz_epochs $pool 7 0:3:1
log_mustnot_expect "bad top-level vdev id" \
    timeout 60 zhack -d $workdir raidz_epochs $pool abc 0:3:1
log_mustnot_expect "bad triplet" \
    timeout 60 zhack -d $workdir raidz_epochs $pool 0 0:3:1x
log_mustnot_expect "not valid" \
    timeout 60 zhack -d $workdir raidz_epochs $pool 0 0:3:2
log_mustnot_expect "not valid" \
    timeout 60 zhack -d $workdir raidz_epochs $pool 0 0:3:1 $far:4:1
log_must eval "zdb -e -p $workdir -m $pool > $workdir/zdb.out"
log_mustnot grep -q raidz_parity_epochs $workdir/zdb.out
log_must zpool import -d $workdir $pool
log_must eval "zpool get -H -o value feature@raidz_parity_epochs $pool | \
    grep -q enabled"
log_must zpool export $pool

# The first entry is the vdev's own geometry; later widths must fit.
typeset -r R_START="the first entry does not start at txg 0"
typeset -r R_PARITY="the first entry does not have the parity of the vdev"
typeset -r R_BASE_WIDTH="the first entry does not have the width of the vdev"
typeset -r R_WIDTH="an entry exceeds the width of the vdev"
typeset -r R_ORDER="the starts of the entries do not increase"
check_refused "a first parity other than the vdev's" "$R_PARITY" 0:3:2
check_refused "a first width other than the vdev's" "$R_BASE_WIDTH" 0:2:1
check_refused "a later width without a data column" \
    "an entry has no data column" 0:3:1 $far:2:2
check_refused "a later width wider than the vdev" "$R_WIDTH" 0:3:1 $far:4:1
check_refused "a first entry that does not cover txg 0" "$R_START" $far:3:1
check_refused "repeated start txgs" "$R_ORDER" 0:3:1 $far:3:2 $far:3:1

# A table that fits, including a future parity-2 epoch, is accepted.
make_pool
log_must zhack -d $workdir raidz_epochs $pool 0 0:3:1 $far:3:2
log_must eval "zdb -e -p $workdir -C $pool > $workdir/zdb.out"
log_must grep -q 'org.openzfs:raidz_parity_epochs\[' $workdir/zdb.out
log_mustnot grep -q com.rrmvp $workdir/zdb.out
log_must zpool import -d $workdir $pool
log_must eval "zpool get -H -o value feature@raidz_parity_epochs $pool | \
    grep -q active"
log_must zpool scrub -w $pool
log_must check_pool_status $pool "errors" "No known data errors"
log_must cmp /$pool/data $workdir/data.ref

log_pass "A parity-epoch table must fit the vdev it is stored on"
