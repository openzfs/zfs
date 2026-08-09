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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
# Copyright (c) 2018 Datto Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy.cfg
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy_common.kshlib

#
# DESCRIPTION:
#	Separately verify 'zfs destroy -f|-r|-rf|-R|-rR <dataset>' will fail in
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

log_assert "Separately verify 'zfs destroy -f|-r|-rf|-R|-rR <dataset>' will " \
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
			log_mustnot zfs destroy $opt $dtst
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
pidlist=$(mkbusy $mntpt/$TESTFILE0)
log_note "mkbusy $mntpt/$TESTFILE0 (pidlist: $pidlist)"
[[ -z $pidlist ]] && log_fail "Failure from mkbusy"
negative_test "-R -rR" $CTR

# The following busy datasets will still exist
check_dataset datasetexists $CTR $FS $VOL

# The following datasets will not exist because of best-effort recursive destroy
if datasetexists $VOLSNAP || datasetexists $FSSNAP; then
	log_must zfs list -rtall
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

log_must kill $pidlist
log_mustnot pgrep -fl mkbusy
pidlist=""

#
# Create the clones for test environment and make the volume busy.
# Then verify 'zfs destroy $CTR' fails without '-f'.
#
# Then verify the expected datasets exist (see below).
#
if is_global_zone; then
	setup_testenv clone
	pidlist=$(mkbusy $TESTDIR1/$TESTFILE0)
	log_note "mkbusy $TESTDIR1/$TESTFILE0 (pidlist: $pidlist)"
	[[ -z $pidlist ]] && log_fail "Failure from mkbusy"
	negative_test "-R -rR" $CTR
	check_dataset datasetexists $CTR $VOL
	check_dataset datasetnonexists $VOLSNAP $VOLCLONE

	# Due to recursive destroy being a best-effort operation,
	# all of the non-busy datasets below should be gone now.
	check_dataset datasetnonexists $FS $FSSNAP $FSCLONE
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

log_must kill $pidlist
log_mustnot pgrep -fl mkbusy
pidlist=""

#
# Create the clones for test environment and make the snapshot busy.
#
# For Linux verify 'zfs destroy $snap' fails due to the busy mount point.  Then
# verify the snapshot remains and the clone was destroyed, but nothing else is.
#
# Under illumos verify 'zfs destroy $snap' succeeds without '-f'.  Then verify
# the snapshot and clone are destroyed, but nothing else is.
#

mntpt=$(snapshot_mountpoint $FSSNAP)
pidlist=$(mkbusy $mntpt)
log_note "mkbusy $mntpt (pidlist: $pidlist)"
[[ -z $pidlist ]] && log_fail "Failure from mkbusy"

for option in -R -rR ; do
	setup_testenv clone

	if is_linux; then
		log_mustnot zfs destroy $option $FSSNAP
		check_dataset datasetexists $CTR $FS $VOL $FSSNAP
		check_dataset datasetnonexists $FSCLONE
	else
		log_must zfs destroy $option $FSSNAP
		check_dataset datasetexists $CTR $FS $VOL
		check_dataset datasetnonexists $FSSNAP $FSCLONE
	fi
done

log_must kill $pidlist
log_mustnot pgrep -fl mkbusy
pidlist=""

log_pass "zfs destroy -f|-r|-rf|-R|-rR <dataset>' failed in different " \
	"condition passed."
