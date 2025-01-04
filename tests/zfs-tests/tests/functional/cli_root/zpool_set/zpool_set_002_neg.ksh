#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# Malformed zpool set commands are rejected
#
# STRATEGY:
#	1. Create an array of many different malformed zfs set arguments
#	2. Run zpool set for each arg checking each will exit with status code 1
#
#

verify_runnable "global"

# note to self - need to make sure there isn't a pool called bootfs
# before running this test...
function cleanup {

	zpool destroy bootfs
	rm $FILEVDEV
}

log_assert "Malformed zpool set commands are rejected"

if poolexists bootfs
then
	log_unsupported "Unable to run test on a machine with a pool called \
 bootfs"
fi

log_onexit cleanup

# build up an array of bad arguments.
set -A arguments "rubbish " \
		"foo@bar= " \
		"@@@= +pool " \
		"zpool bootfs " \
		"bootfs " \
		"bootfs +" \
		"bootfs=bootfs/123 " \
		"bootfs=bootfs@val " \
		"Bootfs=bootfs " \
		"- " \
		"== " \
		"set " \
		"@@ " \
		"12345 " \
		"€にほんご " \
		"/ " \
		"bootfs=bootfs /" \
		"bootfs=a%d%s "


# here, we build up a large string.
# a word to the ksh-wary, ${#array[@]} gives you the
# total number of entries in an array, so array[${#array[@]}]
# will index the last entry+1, ksh arrays start at index 0.
COUNT=0
while [ $COUNT -le 1025 ]
do
	bigname="${bigname}o"
	COUNT=$(( $COUNT + 1 ))
done

# add an argument of maximum length property name
arguments[${#arguments[@]}]="$bigname=value"

# add an argument of maximum length property value
arguments[${#arguments[@]}]="bootfs=$bigname"

# Create a pool called bootfs (so-called, so as to trip any clashes between
# property name, and pool name)
# Also create a filesystem in this pool
FILEVDEV="$TEST_BASE_DIR/zpool_set_002.$$.dat"
log_must mkfile $MINVDEVSIZE $FILEVDEV
log_must zpool create bootfs $FILEVDEV
log_must zfs create bootfs/root

typeset -i i=0;
while [ $i -lt "${#arguments[@]}" ]
do
	log_mustnot eval "zpool set ${arguments[$i]} > /dev/null 2>&1"

	# now also try with a valid pool in the argument list
	log_mustnot eval "zpool set ${arguments[$i]}bootfs > /dev/null 2>&1"

	# now also try with two valid pools in the argument list
	log_mustnot eval "zpool set ${arguments[$i]}bootfs bootfs > /dev/null"
	i=$(( $i + 1))
done

log_pass "Malformed zpool set commands are rejected"
