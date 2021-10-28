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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2011, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zfs inherit' should return an error with bad parameters in one command.
#
# STRATEGY:
# 1. Set an array of bad options and invalid properties to 'zfs inherit'
# 2. Execute 'zfs inherit' with bad options and passing invalid properties
# 3. Verify an error is returned.
#

verify_runnable "both"

function cleanup
{
	snapexists $TESTPOOL/$TESTFS@$TESTSNAP && \
		destroy_dataset $TESTPOOL/$TESTFS@$TESTSNAP
}

log_assert "'zfs inherit' should return an error with bad parameters in" \
    "one command."
log_onexit cleanup

set -A badopts "r" "R" "-R" "-rR" "-a" "-" "-?" "-1" "-2" "-v" "-n"
set -A props "recordsize" "mountpoint" "sharenfs" "checksum" "compression" \
    "atime" "devices" "exec" "setuid" "readonly" "snapdir" "aclmode" \
    "aclinherit" "xattr" "copies"
if is_freebsd; then
	props+=("jailed")
else
	props+=("zoned")
fi
set -A illprops "recordsiz" "mountpont" "sharen" "compres" "atme" "blah"

log_must zfs snapshot $TESTPOOL/$TESTFS@$TESTSNAP

typeset -i i=0
for ds in $TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTFS@$TESTSNAP; do

	# zfs inherit should fail with bad options
	for opt in ${badopts[@]}; do
		for prop in ${props[@]}; do
			log_mustnot eval "zfs inherit $opt $prop $ds \
			    >/dev/null 2>&1"
		done
	done

	# zfs inherit should fail with invalid properties
	for prop in "${illprops[@]}"; do
		log_mustnot eval "zfs inherit $prop $ds >/dev/null 2>&1"
		log_mustnot eval "zfs inherit -r $prop $ds >/dev/null 2>&1"
	done

	# zfs inherit should fail with too many arguments
	(( i = 0 ))
	while (( i < ${#props[*]} -1 )); do
		log_mustnot eval "zfs inherit ${props[(( i ))]} \
				${props[(( i + 1 ))]} $ds >/dev/null 2>&1"
		log_mustnot eval "zfs inherit -r ${props[(( i ))]} \
				${props[(( i + 1 ))]} $ds >/dev/null 2>&1"

		(( i = i + 2 ))
	done

done

# zfs inherit should fail with missing datasets
for prop in ${props[@]}; do
	log_mustnot eval "zfs inherit $prop >/dev/null 2>&1"
	log_mustnot eval "zfs inherit -r $prop >/dev/null 2>&1"
done

log_pass "'zfs inherit' failed as expected when passing illegal arguments."
