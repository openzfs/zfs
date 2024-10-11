#!/bin/ksh -p
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
# Copyright (c) 2023 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/shared_log/shared_log.kshlib

#
# DESCRIPTION:
#	Negative shared log testing.
#
# STRATEGY:
#	1. Attempt to create a client pool with a missing shared log pool
#	2. Attempt to create a client pool with mis-named shared log pool
#	3. Attempt to create a client pool with a shared log and a log device
#	4. Attempt to use a client pool after the shared log has been destroyed
#	5. Attempt to create a client pool when the feature is disabled
#	6. Attempt to export/destroy an active shared log
#	7. Attempt to reguid a client/log pool
#	8. Attempt to checkpoint a client/log pool
#

verify_runnable "global"

log_assert "Negative shared log testing."
log_onexit cleanup

log_mustnot zpool create -f -l $LOGPOOL $TESTPOOL "$DISK0"

log_must zpool create -f $TESTPOOL2 "$DISK2"
log_mustnot zpool create -l $TESTPOOL2 -f $TESTPOOL "$DISK0"
log_must zpool destroy $TESTPOOL2

log_must zpool create -f -L $LOGPOOL "$DISK0"
log_mustnot zpool create -f -l "${LOGPOOL}2" $TESTPOOL "$DISK1" 
log_mustnot zpool create -f -l $LOGPOOL $TESTPOOL "$DISK1" log "$DISK2"

log_must zpool create -f -L ${LOGPOOL}2 "$DISK1"
log_must zpool destroy ${LOGPOOL}2

typeset FS="$LOGPOOL/fs"
log_mustnot zfs create -o sync=always -o recordsize=8k $FS

log_mustnot zpool create -f -l $LOGPOOL -o feature@shared_log=disabled $TESTPOOL "$DISK1"
log_mustnot zpool create -f -L -o feature@shared_log=disabled ${LOGPOOL}2 "$DISK1"

log_must zpool create -f -l $LOGPOOL $TESTPOOL "$DISK1"
log_mustnot zpool export $LOGPOOL
log_mustnot zpool destroy $LOGPOOL

log_mustnot zpool reguid $LOGPOOL
log_mustnot zpool reguid $TESTPOOL

log_mustnot zpool checkpoint $TESTPOOL
log_mustnot zpool checkpoint $LOGPOOL

log_pass "Negative shared log testing."
