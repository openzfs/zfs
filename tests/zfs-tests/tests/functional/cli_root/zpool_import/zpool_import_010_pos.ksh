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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg

#
# DESCRIPTION:
#	'zpool -D -a' can import all the specified directories destroyed pools.
#
# STRATEGY:
#	1. Create a 5 ways mirror pool A with dev0/1/2/3/4, then destroy it.
#	2. Create a stripe pool B with dev1. Then destroy it.
#	3. Create a draid2 pool C with dev2/3/4/5. Then destroy it.
#	4. Create a raidz pool D with dev3/4. Then destroy it.
#	5. Create a stripe pool E with dev4. Then destroy it.
#	6. Verify 'zpool import -D -a' recover all the pools.
#

verify_runnable "global"

function cleanup
{
	typeset dt
	for dt in $poolE $poolD $poolC $poolB $poolA; do
		destroy_pool $dt
	done

	log_must rm -rf $DEVICE_DIR/*
	typeset i=0
	while (( i < $MAX_NUM )); do
		log_must mkfile $FILE_SIZE ${DEVICE_DIR}/${DEVICE_FILE}$i
		((i += 1))
	done
}

log_assert "'zpool -D -a' can import all the specified directories " \
	"destroyed pools."
log_onexit cleanup

poolA=poolA.$$; poolB=poolB.$$; poolC=poolC.$$; poolD=poolD.$$; poolE=poolE.$$

log_must zpool create $poolA mirror $VDEV0 $VDEV1 $VDEV2 $VDEV3 $VDEV4
log_must zpool destroy $poolA

log_must zpool create $poolB $VDEV1
log_must zpool destroy $poolB

log_must zpool create $poolC draid2 $VDEV2 $VDEV3 $VDEV4 $VDEV5
log_must zpool destroy $poolC

log_must zpool create $poolD raidz $VDEV3 $VDEV4
log_must zpool destroy $poolD

log_must zpool create $poolE $VDEV4
log_must zpool destroy $poolE

log_must zpool import -d $DEVICE_DIR -D -f -a

for dt in $poolA $poolB $poolC $poolD $poolE; do
	log_must datasetexists $dt
done

log_pass "'zpool -D -a' test passed."
