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

#
# DESCRIPTION:
#	"zfs snapshot" fails with bad options,too many arguments or too long
#	snapshot name
#
# STRATEGY:
#	1. Create an array of invalid arguments
#	2. Execute 'zfs snapshot' with each argument in the array,
#	3. Verify an error is returned.
#

verify_runnable "both"

log_assert "'zfs snapshot' fails with bad options, or too many arguments. "

set -A badopts "r" "R" "-R" "-x" "-rR" "-?" "-*" "-123"

# set too long snapshot name (>256)
l_name="$(gen_dataset_name 260 abcdefg)"

for ds in $TESTPOOL/$TESTFS $TESTPOOL/$TESTCTR $TESTPOOL/$TESTVOL; do
	for opt in ${badopts[@]}; do
		log_mustnot zfs snapshot $opt $ds@$TESTSNAP
	done

	log_mustnot zfs snapshot $ds@snap $ds@snap1
	log_mustnot zfs snapshot -r $ds@snap $ds@snap1

	log_mustnot zfs snapshot $ds@$l_name
	log_mustnot zfs snapshot -r $ds@$l_name
done

log_pass "'zfs snapshot' fails with bad options or too many arguments as expected."
