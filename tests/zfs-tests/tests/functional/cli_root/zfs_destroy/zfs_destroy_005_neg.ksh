#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy.cfg
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy_common.kshlib

#
# DESCRIPTION:
#	Seperately verify 'zfs destroy -f|-r|-rf|-R|-rR <dataset>' will fail in
#       different conditions.
#
# STRATEGY:
#	1. Create pool, fs & vol.
#	2. Create snapshot for fs & vol.
#	3. Invoke 'zfs destroy ''|-f <dataset>', it should fail.
#	4. Create clone for fs & vol.
#	5. Invoke 'zfs destroy -r|-rf <dataset>', it should fail.
#	6. Write file to filesystem or enter snapshot mountpoint.
#	7. Invoke 'zfs destroy -R|-rR <dataset>', it should fail.
#

verify_runnable "both"

log_assert "Seperately verify 'zfs destroy -f|-r|-rf|-R|-rR <dataset>' will " \
	"fail in different conditions."
log_onexit cleanup_testenv

#
# Run 'zfs destroy [-rRf] <dataset>', make sure it fail.
#
# $1 the collection of options
# $2 the collection of datasets
#
function negative_test
{
	typeset options=$1
	typeset datasets=$2

	$PKILL mkbusy

	for dtst in $datasets; do
		if ! is_global_zone; then
			if [[ $dtst == $VOL || $dtst == $VOLSNAP || \
				$dtst == $VOLCLONE ]]
			then
				log_note "UNSUPPORTED: " \
					"Volume is unavailable in LZ."
				continue
			fi
		fi
		for opt in $options; do
			log_mustnot $ZFS destroy $opt $dtst
		done
	done
}

#
# Create snapshots for filesystem and volume,
# and verify 'zfs destroy' fails without '-r' or '-R'.
#
setup_testenv snap
negative_test "-f" "$CTR $FS $VOL"

#
# Create clones for filesystem and volume,
# and verify 'zfs destroy' fails without '-R'.
#
setup_testenv clone
negative_test "-r -rf" "$CTR $FS $VOL"

#
# Get $FS mountpoint and make it busy, and verify 'zfs destroy $CTR' fails
# without '-f'. Then verify the remaining datasets are correct. See below for
# an explanation of what 'correct' means for this test.
#
mntpt=$(get_prop mountpoint $FS)
pidlist=$($MKBUSY $mntpt/$TESTFILE0)
log_note "$MKBUSY $mntpt/$TESTFILE0 (pidlist: $pidlist)"
[[ -z $pidlist ]] && log_fail "Failure from mkbusy"
negative_test "-R -rR" $CTR

#
# Checking the outcome of the test above is tricky, because the order in
# which datasets are destroyed is not deterministic. Both $FS and $VOL are
# busy, and the remaining datasets will be different depending on whether we
# tried (and failed) to delete $FS or $VOL first.

# The following datasets will exist independent of the order
check_dataset datasetexists $CTR $FS $VOL

if datasetexists $VOLSNAP && datasetnonexists $FSSNAP; then
	# The recursive destroy failed on $FS
	check_dataset datasetnonexists $FSSNAP $FSCLONE
	check_dataset datasetexists $VOLSNAP $VOLCLONE
elif datasetexists $FSSNAP && datasetnonexists $VOLSNAP; then
	# The recursive destroy failed on $VOL
	check_dataset datasetnonexists $VOLSNAP $VOLCLONE
	check_dataset datasetexists $FSSNAP $FSCLONE
else
	log_must $ZFS list -rtall
	log_fail "Unexpected datasets remaining"
fi

#
# Create the clones for test environment, and verify 'zfs destroy $FS' fails
# without '-f'.  Then verify the FS snap and clone are the only datasets
# that were removed.
#
setup_testenv clone
negative_test "-R -rR" $FS
check_dataset datasetexists $CTR $FS $VOL $VOLSNAP $VOLCLONE
check_dataset datasetnonexists $FSSNAP $FSCLONE

log_must $KILL $pidlist
log_mustnot $PGREP -fl $MKBUSY
pidlist=""

#
# Create the clones for test environment and make the volume busy.
# Then verify 'zfs destroy $CTR' fails without '-f'.
#
# Then verify the expected datasets exist (see below).
#
if is_global_zone; then
	setup_testenv clone
	pidlist=$($MKBUSY $TESTDIR1/$TESTFILE0)
	log_note "$MKBUSY $TESTDIR1/$TESTFILE0 (pidlist: $pidlist)"
	[[ -z $pidlist ]] && log_fail "Failure from mkbusy"
	negative_test "-R -rR" $CTR
	check_dataset datasetexists $CTR $VOL
	check_dataset datasetnonexists $VOLSNAP $VOLCLONE

	# Here again, the non-determinism of destroy order is a factor. $FS,
	# $FSSNAP and $FSCLONE will still exist here iff we attempted to destroy
	# $VOL (and failed) first. So check that either all of the datasets are
	# present, or they're all gone.
	if datasetexists $FS; then
		check_dataset datasetexists $FS $FSSNAP $FSCLONE
	else
		check_dataset datasetnonexists $FS $FSSNAP $FSCLONE
	fi
fi

#
# Create the clones for test environment and make the volume busy.
# Then verify 'zfs destroy $VOL' fails without '-f'.
#
# Then verify the snapshot and clone are destroyed, but nothing else is.
#
if is_global_zone; then
	setup_testenv clone
	negative_test "-R -rR" $VOL
	check_dataset datasetexists $CTR $VOL $FS $FSSNAP $FSCLONE
	check_dataset datasetnonexists $VOLSNAP $VOLCLONE
fi

log_must $KILL $pidlist
log_mustnot $PGREP -fl $MKBUSY
pidlist=""

#
# Create the clones for test environment and make the snapshot busy.
# Then verify 'zfs destroy $snap' succeeds without '-f'.
#
# Then verify the snapshot and clone are destroyed, but nothing else is.
#

mntpt=$(snapshot_mountpoint $FSSNAP)
pidlist=$($MKBUSY $mntpt)
log_note "$MKBUSY $mntpt (pidlist: $pidlist)"
[[ -z $pidlist ]] && log_fail "Failure from mkbusy"

for option in -R -rR ; do
	setup_testenv clone
	destroy_dataset $option $FSSNAP
	check_dataset datasetexists $CTR $FS $VOL
	check_dataset datasetnonexists $FSSNAP $FSCLONE
done

log_must $KILL $pidlist
log_mustnot $PGREP -fl $MKBUSY
pidlist=""

log_pass "zfs destroy -f|-r|-rf|-R|-rR <dataset>' failed in different " \
	"condition passed."
