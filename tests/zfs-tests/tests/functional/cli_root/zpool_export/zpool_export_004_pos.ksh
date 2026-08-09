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

. $STF_SUITE/tests/functional/cli_root/zpool_export/zpool_export.kshlib

#
# DESCRIPTION:
#	Verify zpool export succeed or fail with spare.
#
# STRATEGY:
#	1. Create two mirror pools with same spare.
#	2. Verify zpool export one pool succeed.
#	3. Import the pool.
#	4. Replace one device with the spare and detach it in one pool.
#	5. Verify zpool export the pool succeed.
#	6. Import the pool.
#	7. Replace one device with the spare in one pool.
#	8. Verify zpool export the pool fail.
#	9. Verify zpool export the pool with "-f" succeed.
#	10. Import the pool.
#

verify_runnable "global"

log_assert "Verify zpool export succeed or fail with spare."
log_onexit zpool_export_cleanup

mntpnt=$TESTDIR0
log_must mkdir -p $mntpnt

# mntpnt=$(get_prop mountpoint $TESTPOOL)

typeset -i i=0
while ((i < 5)); do
	log_must truncate -s $MINVDEVSIZE $mntpnt/vdev$i
	eval vdev$i=$mntpnt/vdev$i
	((i += 1))
done

log_must zpool create $TESTPOOL1 mirror $vdev0 $vdev1 spare $vdev4
log_must zpool create $TESTPOOL2 mirror $vdev2 $vdev3 spare $vdev4

log_must zpool export $TESTPOOL1
log_must zpool import -d $mntpnt $TESTPOOL1

log_must zpool replace $TESTPOOL1 $vdev0 $vdev4
log_must zpool detach $TESTPOOL1 $vdev4
log_must zpool export $TESTPOOL1
log_must zpool import -d $mntpnt $TESTPOOL1

log_must zpool replace $TESTPOOL1 $vdev0 $vdev4
log_mustnot zpool export $TESTPOOL1

log_must zpool export -f $TESTPOOL1
log_must zpool import -d $mntpnt  $TESTPOOL1

log_pass "Verify zpool export succeed or fail with spare."

