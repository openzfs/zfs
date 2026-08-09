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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	'zfs rename' should be failed with bad option, null target dataset,
#	too many datasets and long target dataset name.
#
# STRATEGY:
#	1. Create a set of ZFS datasets;
#	2. Try 'zfs rename' with various illegal scenarios;
#	3. Verify 'zfs rename' command should be failed.
#

verify_runnable "both"

log_assert "'zfs rename' should fail with bad option, null target dataset and" \
		"too long target dataset name."

badopts=( "r" "R" "-R" "-rR" "-Rr" "-P" "-pP" "-Pp" "-r*" "-p*" "-?" "-*" "-"
    "-o")
datasets=("$TESTPOOL" "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTFS@$TESTSNAP"
    "$TESTPOOL/$TESTCTR" "$TESTPOOL/$TESTCTR/$TESTFS1" "$TESTPOOL/$TESTVOL")

longname="$(gen_dataset_name 260 abcdefg)"

log_must zfs snapshot $TESTPOOL/$TESTFS@$TESTSNAP
for ds in ${datasets[@]}; do
	for opt in ${badopts[@]}; do
		log_mustnot zfs rename $opt $ds ${ds}-new
	done
	log_mustnot zfs rename $ds
	log_mustnot zfs rename $ds ${ds}-new ${ds}-new1
	log_mustnot zfs rename $ds ${ds}.$longname
done

log_pass "'zfs rename' fails with illegal scenarios as expected."
