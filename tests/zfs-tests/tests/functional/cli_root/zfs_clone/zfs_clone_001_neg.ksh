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
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	'zfs clone' should fail with inapplicable scenarios, including:
#		* Null arguments
#		* non-existent snapshots.
#		* invalid characters in ZFS namesapec
#		* Leading slash in the target clone name
#		* The argument contains an empty component.
#		* The pool specified in the target doesn't exist.
#		* The parent dataset of the target doesn't exist.
#		* The argument refer to a pool, not dataset.
#		* The target clone already exists.
#		* Null target clone argument.
#		* Too many arguments.
#               * Invalid record sizes.
#
# STRATEGY:
#	1. Create an array of parameters
#	2. For each parameter in the array, execute the sub-command
#	3. Verify an error is returned.
#

verify_runnable "both"

typeset target1=$TESTPOOL/$TESTFS1
typeset target2=$TESTPOOL/$TESTCTR1/$TESTFS1
typeset targets="$target1 $target2 $NONEXISTPOOLNAME/$TESTFS"

set -A args "" \
	"$TESTPOOL/$TESTFS@blah $target1" "$TESTPOOL/$TESTVOL@blah $target1" \
	"$TESTPOOL/$TESTFS@blah* $target1" "$TESTPOOL/$TESTVOL@blah* $target1" \
	"$SNAPFS $target1*" "$SNAPFS1 $target1*" \
	"$SNAPFS /$target1" "$SNAPFS1 /$target1" \
	"$SNAPFS $TESTPOOL//$TESTFS1" "$SNAPFS1 $TESTPOOL//$TESTFS1" \
	"$SNAPFS $NONEXISTPOOLNAME/$TESTFS" "$SNAPFS1 $NONEXISTPOOLNAME/$TESTFS" \
	"$SNAPFS" "$SNAPFS1" \
	"$SNAPFS $target1 $target2" "$SNAPFS1 $target1 $target2" \
        "-o recordsize=2M $SNAPFS1 $target1" \
        "-o recordsize=128B $SNAPFS1 $target1"
typeset -i argsnum=${#args[*]}
typeset -i j=0
while (( j < argsnum )); do
	args[((argsnum+j))]="-p ${args[j]}"
	((j = j + 1))
done

set -A moreargs "$SNAPFS $target2" "$SNAPFS1 $target2" \
	"$SNAPFS $TESTPOOL" "$SNAPFS1 $TESTPOOL" \
	"$SNAPFS $TESTPOOL/$TESTCTR" "$SNAPFS $TESTPOOL/$TESTFS" \
	"$SNAPFS1 $TESTPOOL/$TESTCTR" "$SNAPFS1 $TESTPOOL/$TESTFS"

set -A args ${args[*]} ${moreargs[*]}

function setup_all
{
	log_note "Create snapshots and mount them..."

	for snap in $SNAPFS $SNAPFS1 ; do
		if ! snapexists $snap ; then
			log_must zfs snapshot $snap
		fi
	done

	return 0
}

function cleanup_all
{
	for fs in $targets; do
		datasetexists $fs && destroy_dataset $fs -f
	done

	for snap in $SNAPFS $SNAPFS1 ; do
		snapexists $snap && destroy_dataset $snap -Rf
	done

	return 0
}

log_assert "Badly-formed 'zfs clone' with inapplicable scenarios" \
	"should return an error."
log_onexit cleanup_all

setup_all

typeset -i i=0
while (( i < ${#args[*]} )); do
	log_mustnot zfs clone ${args[i]}
	((i = i + 1))
done

log_pass "Badly formed 'zfs clone' with inapplicable scenarios" \
	"fail as expected."
