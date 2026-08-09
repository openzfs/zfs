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
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy.cfg

#
# DESCRIPTION:
#	'zfs destroy <filesystem|volume|snapshot>' can successfully destroy
#	the specified dataset which has no active dependents.
#
# STRATEGY:
#	1. Create a filesystem,volume and snapshot in the storage pool
#	2. Destroy the filesystem,volume and snapshot
#	3. Verify the datasets are destroyed successfully
#

verify_runnable "both"

function cleanup
{
	typeset -i i=0
	while (( $i < ${#data_objs[*]} )); do
		destroy_dataset "${data_objs[i]}" "-rf"
		((i = i + 1))
	done
}

log_assert "Verify 'zfs destroy' can destroy the specified datasets without active" \
	"dependents."
log_onexit cleanup

if is_global_zone ; then
	set -A data_objs "$TESTPOOL/$TESTFS@$TESTSNAP" "$TESTPOOL/$TESTFS1" \
		"$TESTPOOL/$TESTVOL" "$TESTPOOL/$TESTVOL1"
else
	set -A data_objs "$TESTPOOL/$TESTFS@$TESTSNAP" "$TESTPOOL/$TESTFS1"
fi

log_must zfs create $TESTPOOL/$TESTFS1
log_must zfs snapshot $TESTPOOL/$TESTFS@$TESTSNAP

if is_global_zone ; then
	log_must zfs create -V $VOLSIZE $TESTPOOL/$TESTVOL

	# Max volume size is 1TB on 32-bit systems
	[[ is_32bit ]] && \
		BIGVOLSIZE=1Tb
	log_must zfs create -sV $BIGVOLSIZE $TESTPOOL/$TESTVOL1
fi

typeset -i i=0
while (( $i < ${#data_objs[*]} )); do
	datasetexists ${data_objs[i]} || \
		log_fail "Create <filesystem>|<volume>|<snapshot> fail."
	((i = i + 1))
done

i=0
while (( $i < ${#data_objs[*]} )); do
	destroy_dataset "${data_objs[i]}"
	datasetexists ${data_objs[i]} && \
		log_fail "'zfs destroy <filesystem>|<volume>|<snapshot>' fail."
	((i = i + 1))
done

log_pass "'zfs destroy <filesystem>|<volume>|<snapshot>' works as expected."
