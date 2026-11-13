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
#	Blocks born on a pool that was expanded while
#	raidz_expansion_accounting was enabled but not active are freed
#	with the deflation ratio they were charged with, after a later
#	expansion activates the feature.
#
#	Software without the feature charges every block at the legacy
#	ratio.  When a later expansion activates the feature, blocks born
#	before the activation must still be freed at the legacy ratio.
#	Otherwise the free releases more space than was charged, and the
#	dataset's used space drops below what it holds.
#
# STRATEGY:
#	1. Import the stored pool and check that the feature is enabled
#	   but not active.
#	2. Expand the RAID-Z vdev to five devices, which activates the
#	   feature, and check that it is active.
#	3. Delete the file written before the activation and sync.
#	4. Require the dataset's used space to be back at its baseline,
#	   both before and after an export and import.
#
# The pool was created by OpenZFS with this feature from three 64 MiB
# files with "zpool create -o ashift=12 -O compression=off
# -O recordsize=128k late_activation raidz1 ...", followed by
# "zfs create late_activation/fs", leaving the feature enabled and fs at
# 130944 bytes used.  It was then imported by OpenZFS without the feature,
# expanded to four devices with "zpool attach -w", and fs/file was written
# with 32 MiB of 'L'.
#

verify_runnable "global"

typeset -r pool=late_activation
typeset -r fs=$pool/fs
typeset -ri baseline=130944

function cleanup
{
	poolexists $pool && destroy_pool $pool
	rm -rf $workdir
}

#
# Allow for small metadata changes.  A born/free mismatch frees several
# megabytes more than was charged for the file.
#
function check_used
{
	typeset -ri tolerance=$((32 * 1024))
	typeset used

	used=$(get_prop used $fs)

	log_note "Used: $used (baseline $baseline)"
	if (( used < baseline - tolerance || used > baseline + tolerance ))
	then
		log_fail "Dataset used ($used) differs from baseline" \
		    "($baseline) by more than $tolerance bytes"
	fi
}

log_assert "Blocks born before a late activation of" \
    "raidz_expansion_accounting are freed consistently"
workdir=$(mktemp -d $TEST_BASE_DIR/raidz_expand_019.XXXXXX) ||
    log_fail "cannot create test directory"
log_onexit cleanup

typeset -r blockfiles=$STF_SUITE/tests/functional/raidz/blockfiles
for i in 0 1 2 3; do
	log_must eval "bzcat $blockfiles/raidz_expand_019-$i.dat.bz2" \
	    "> $workdir/$pool-$i.dat"
done
log_must truncate -s 64M $workdir/$pool-4.dat
log_must eval "head -c $((32 * 1024 * 1024)) /dev/zero | tr '\\0' L" \
    "> $workdir/file"

log_must zpool import -d $workdir $pool
log_must test "$(get_pool_prop feature@raidz_expansion_accounting $pool)" \
    == "enabled"
log_must cmp $workdir/file /$fs/file

log_must zpool attach -w $pool raidz1-0 $workdir/$pool-4.dat
log_must test "$(get_pool_prop feature@raidz_expansion_accounting $pool)" \
    == "active"

log_must rm /$fs/file
log_must zpool sync $pool
check_used

log_must zpool export $pool
log_must zpool import -d $workdir $pool
check_used

log_pass "Blocks born before a late activation of" \
    "raidz_expansion_accounting are freed consistently"
