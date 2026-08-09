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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/refreserv/refreserv.cfg

#
# DESCRIPTION:
#	Setting full size as refreservation, verify no snapshot can be created.
#
# STRATEGY:
#	1. Setting full size as refreservation on pool
#	2. Verify no snapshot can be created on this pool
#	3. Setting full size as refreservation on filesystem
#	4. Verify no snapshot can be created on it and its subfs
#

verify_runnable "both"

function cleanup
{
	if is_global_zone ; then
		log_must zfs set refreservation=none $TESTPOOL

		datasetexists $TESTPOOL@snap && destroy_dataset $TESTPOOL@snap -f
	fi
	destroy_dataset $TESTPOOL/$TESTFS -rf
	log_must zfs create $TESTPOOL/$TESTFS
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

# This function iteratively increases refreserv to its highest possible
# value. Simply setting refreserv == quota can allow enough writes to
# complete that the test fails.
function max_refreserv
{
	typeset ds=$1
	typeset -i incsize=131072
	typeset -i rr=$(get_prop available $ds)

	log_must zfs set refreserv=$rr $ds
	while :; do
		if zfs set refreserv=$((rr + incsize)) $ds >/dev/null 2>&1; then
			((rr += incsize))
			continue
		else
			((incsize /= 2))
			((incsize == 0)) && break
		fi
	done
}


log_assert "Setting full size as refreservation, verify no snapshot " \
	"can be created."
log_onexit cleanup

log_must zfs create $TESTPOOL/$TESTFS/subfs

typeset datasets
if is_global_zone; then
	datasets="$TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTFS/subfs"
else
	datasets="$TESTPOOL/$TESTFS $TESTPOOL/$TESTFS/subfs"
fi

for ds in $datasets; do
	#
	# Verify refreservation on dataset
	#
	log_must zfs set quota=25M $ds
	max_refreserv $ds
	log_mustnot zfs snapshot $ds@snap
	if datasetexists $ds@snap ; then
		log_fail "ERROR: $ds@snap should not exists."
	fi

	log_must zfs set quota=none $ds
	log_must zfs set refreservation=none $ds
done

log_pass "Setting full size as refreservation, verify no snapshot " \
	"can be created."
