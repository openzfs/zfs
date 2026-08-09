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
