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
. $STF_SUITE/tests/functional/cli_root/zfs_snapshot/zfs_snapshot.cfg

#
# DESCRIPTION:
#	Try each 'zfs snapshot' with inapplicable scenarios to make sure
#	it returns an error. include:
#		* No arguments given.
#		* The argument contains invalid characters for the ZFS namesapec
#		* Leading slash in snapshot name
#		* The argument contains an empty component.
#		* Missing '@' delimiter.
#		* Multiple '@' delimiters in snapshot name.
#		* The snapshot already exist.
#		* Create snapshot upon the pool.
#			(Be removed since pool is treated as filesystem as well)
#		* Create snapshot upon a non-existent filesystem.
#		* Too many arguments.
#
# STRATEGY:
#	1. Create an array of parameters
#	2. For each parameter in the array, execute the sub-command
#	3. Verify an error is returned.
#

verify_runnable "both"

set -A args "" \
	"$TESTPOOL/$TESTFS@blah*" "$TESTPOOL/$TESTFS@blah?" \
	"$TESTPOOL/$TESTVOL@blah*" "$TESTPOOL/$TESTVOL@blah?" \
	"/$TESTPOOL/$TESTFS@$TESTSNAP" "/$TESTPOOL/$TESTVOL@$TESTSNAP" \
	"@$TESTSNAP" "$TESTPOOL/$TESTFS@" "$TESTPOOL/$TESTVOL@" \
	"$TESTPOOL//$TESTFS@$TESTSNAP" "$TESTPOOL//$TESTVOL@$TESTSNAP" \
	"$TESTPOOL/$TESTFS/$TESTSNAP" "$TESTPOOL/$TESTVOL/$TESTSNAP" \
	"$TESTPOOL/$TESTFS@$TESTSNAP@$TESTSNAP1" \
	"$TESTPOOL/$TESTVOL@$TESTSNAP@$TESTSNAP1" \
	"$SNAPFS" "$SNAPFS1" \
	"blah/blah@$TESTSNAP"



function setup_all
{
	log_note "Create snapshots and mount them..."

	for snap in $SNAPFS $SNAPFS1; do
		if ! snapexists $snap; then
			log_must zfs snapshot $snap
		fi
	done

	return 0
}

function cleanup_all
{
	typeset -i i=0

	while (( i < ${#args[*]} )); do
		for snap in ${args[i]}; do
			snapexists $snap && destroy_dataset $snap -f
		done
		(( i = i + 1 ))
	done

	for mtpt in $SNAPDIR $SNAPDIR1; do
		[[ -d $mtpt ]] && log_must rm -rf $mtpt
	done

	return 0
}

log_assert "Badly-formed 'zfs snapshot' with inapplicable scenarios " \
	"should return an error."
log_onexit cleanup_all

setup_all

typeset -i i=0
while (( i < ${#args[*]} )); do
	log_mustnot zfs snapshot ${args[i]}
	((i = i + 1))
done

log_pass "Badly formed 'zfs snapshot' with inapplicable scenarios " \
	"fail as expected."
