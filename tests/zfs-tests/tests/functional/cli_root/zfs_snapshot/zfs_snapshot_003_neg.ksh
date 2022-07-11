#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
