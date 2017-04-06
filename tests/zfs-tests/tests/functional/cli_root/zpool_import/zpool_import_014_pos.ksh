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
# Copyright 2016, loli10K. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg

#
# DESCRIPTION:
#	Temporary pool names should not be persisted on devices.
#
# STRATEGY:
#	1. Create pool A, then export it.
#	2. Re-import the pool with a temporary name B, then export it.
#	3. Verify device labels still contain the expected pool name (A).
#

verify_runnable "global"

function cleanup
{
	typeset dt
	for dt in $poolB $poolA; do
		destroy_pool $dt
	done

	log_must rm -rf $DEVICE_DIR/*
	typeset i=0
	while (( i < $MAX_NUM )); do
		log_must mkfile $FILE_SIZE ${DEVICE_DIR}/${DEVICE_FILE}$i
		((i += 1))
	done
}

#
# Verify name of (exported) pool from device $1 label is equal to $2
# $1 device
# $2 pool name
#
function verify_pool_name
{
	typeset device=$1
	typeset poolname=$2
	typeset labelname

	zdb -e -l $device | grep " name:" | {
		while read labelname ; do
			if [[ "name: '$poolname'" != "$labelname" ]]; then
				return 1
			fi
		done
	}
	return 0
}

log_assert "Temporary pool names should not be persisted on devices."
log_onexit cleanup

poolA=poolA.$$; poolB=poolB.$$;

log_must zpool create $poolA $VDEV0
log_must zpool export $poolA

log_must zpool import -t $poolA $poolB -d $DEVICE_DIR
log_must zpool export $poolB

log_must eval "verify_pool_name $VDEV0 $poolA"

log_pass "Temporary pool names are not persisted on devices."
