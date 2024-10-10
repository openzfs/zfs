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
#	Client pools can be reimported without provider, with flag.
#
# STRATEGY:
#	1. Create shared log pools & client
#	2. Write data to client
#	3. Export client and provider
#	4. Import client with -m
#	5. Export client
#	6. Import client with -m and new provider
#

verify_runnable "global"

log_assert "Client pools can be reimported without provider, with flag."
log_onexit cleanup

typeset FS="$TESTPOOL/fs"

log_must create_pool $LOGPOOL -L "$DISK0"
log_must create_pool ${LOGPOOL}2 -L "$DISK1"
log_must create_pool $TESTPOOL -l $LOGPOOL "$DISK2"
log_must verify_shared_log $TESTPOOL $LOGPOOL
log_must zfs create -o sync=always -o recordsize=8k $FS
mntpnt=$(get_prop mountpoint $FS)

log_must dd if=/dev/urandom of="$mntpnt/f1" bs=8k count=128
log_must zpool export $TESTPOOL
log_must zpool export $LOGPOOL
log_must zpool import -m $TESTPOOL
log_must dd if=/dev/urandom of="$mntpnt/f2" bs=8k count=128
log_must zpool export $TESTPOOL
log_must zpool import $LOGPOOL
log_must zpool import -m -L ${LOGPOOL}2 $TESTPOOL
log_must verify_shared_log $TESTPOOL ${LOGPOOL}2
log_must dd if=/dev/urandom of="$mntpnt/f3" bs=8k count=128
verify_pool $LOGPOOL
verify_pool $LOGPOOL2
verify_pool $TESTPOOL

log_pass "Client pools can be reimported without provider, with flag."
