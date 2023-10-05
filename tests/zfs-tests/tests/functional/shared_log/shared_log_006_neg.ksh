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
#	1. Create shared log pool & client
#	2. Write some data to the client pool
#	3. Scrub client and provider pools
#

verify_runnable "global"

log_assert "Negative shared log testing."
log_onexit cleanup

log_mustnot create_pool $TESTPOOL -l $LOGPOOL "$DISK0"

log_must create_pool $TESTPOOL2 "$DISK2"
log_mustnot create_pool $TESTPOOL -l $TESTPOOL2 "$DISK0"
log_must zpool destroy $TESTPOOL2

log_must create_pool $LOGPOOL -L "$DISK0"
log_mustnot create_pool $TESTPOOL -l "${LOGPOOL}2" "$DISK1" 
log_mustnot create_pool $TESTPOOL -l $LOGPOOL "$DISK1" log "$DISK2"

log_must create_pool ${LOGPOOL}2 -L "$DISK1"
log_must zpool destroy ${LOGPOOL}2

typeset FS="$LOGPOOL/fs"
log_mustnot zfs create -o sync=always -o recordsize=8k $FS

log_mustnot create_pool $TESTPOOL -l $LOGPOOL -o feature@shared_log=disabled "$DISK1"
log_mustnot create_pool ${LOGPOOL}2 -L -o feature@shared_log=disabled "$DISK1"

log_must create_pool $TESTPOOL -l $LOGPOOL "$DISK1"
log_mustnot zpool export $LOGPOOL
log_mustnot zpool destroy $LOGPOOL

log_mustnot zpool reguid $LOGPOOL
log_mustnot zpool reguid $TESTPOOL

log_mustnot zpool checkpoint $TESTPOOL
log_mustnot zpool checkpoint $LOGPOOL

log_pass "Negative shared log testing."
