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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_rename/zfs_rename.kshlib

#
# DESCRIPTION:
#       'zfs rename' should fail when this dataset was changed to an existed
#	dataset name or datasets are of different types.
#       For example, a filesystem cannot be renamed as a volume.
#
# STRATEGY:
#       1. Given a file system, snapshot and volume.
#       2. Rename each dataset object to a different type.
#       3. Verify that only the original name is displayed by zfs list.
#

verify_runnable "both"

#
# This array is a list of pairs:
#	item i: original type
#	item i + 1: new type
#
set -A bad_dataset $TESTPOOL/$TESTFS1 $TESTPOOL/$TESTCTR1 \
	$TESTPOOL/$TESTFS1 $TESTPOOL/$TESTCTR/$TESTFS1 \
	$TESTPOOL/$TESTFS1 $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS1/$TESTFS1 \
	$TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS@snapshot \
	$TESTPOOL/$TESTCTR1 $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTCTR1 $TESTPOOL/$TESTFS@snapshot \
	$TESTPOOL/$TESTCTR1 $TESTPOOL/$TESTFS1 \
	$TESTPOOL/$TESTCTR1 $TESTPOOL/$TESTCTR/$TESTFS1 \
	$TESTPOOL/$TESTCTR/$TESTFS1  $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTCTR/$TESTFS1  $TESTPOOL/$TESTFS@snapshot \
	$TESTPOOL/$TESTCTR/$TESTFS1  $TESTPOOL/$TESTFS1 \
	$TESTPOOL/$TESTCTR/$TESTFS1  $TESTPOOL/$TESTCTR1 \
	$TESTPOOL/$TESTVOL $TESTPOOL/$TESTCTR1 \
	$TESTPOOL/$TESTVOL $TESTPOOL/$TESTFS@snapshot \
	$TESTPOOL/$TESTVOL $TESTPOOL/$TESTFS1 \
	$TESTPOOL/$TESTVOL $TESTPOOL/$TESTCTR/$TESTFS1 \
	$TESTPOOL/$TESTFS@snapshot $TESTPOOL/$TESTCTR1 \
	$TESTPOOL/$TESTFS@snapshot $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTFS@snapshot $TESTPOOL/$TESTFS1 \
	$TESTPOOL/$TESTFS@snapshot $TESTPOOL/$TESTCTR/$TESTFS1 \
	$TESTPOOL/$TESTFS1 $TESTPOOL/${TESTFS1}%c \
	$TESTPOOL/$TESTFS1 $TESTPOOL/${TESTFS1}%d \
	$TESTPOOL/$TESTFS1 $TESTPOOL/${TESTFS1}%x \
	$TESTPOOL/$TESTFS1 $TESTPOOL/${TESTFS1}%p \
	$TESTPOOL/$TESTFS1 $TESTPOOL/${TESTFS1}%s \
	$TESTPOOL/$TESTFS@snapshot $TESTPOOL/$TESTFS@snapshot/fs \
	$TESTPOOL/$RECVFS/%recv $TESTPOOL/renamed.$$

#
# cleanup defined in zfs_rename.kshlib
#
log_onexit cleanup

log_assert "'zfs rename' should fail when datasets are of a different type."

additional_setup

typeset -i i=0
while ((i < ${#bad_dataset[*]} )); do
        log_mustnot zfs rename ${bad_dataset[i]} ${bad_dataset[((i + 1))]}
        log_must datasetexists ${bad_dataset[i]}

        log_mustnot zfs rename -p ${bad_dataset[i]} ${bad_dataset[((i + 1))]}
        log_must datasetexists ${bad_dataset[i]}

	((i = i + 2))
done

#verify 'rename -p' can not work with snapshots

log_mustnot zfs rename -p $TESTPOOL/$TESTFS@snapshot \
		$TESTPOOL/$TESTFS@snapshot2
log_must datasetexists $TESTPOOL/$TESTFS@snapshot
log_mustnot zfs rename -p $TESTPOOL/$TESTFS@snapshot \
		$TESTPOOL/$TESTFS/$TESTFS@snapshot2
log_must datasetexists $TESTPOOL/$TESTFS@snapshot

log_pass "'zfs rename' fails as expected when given different dataset types."
