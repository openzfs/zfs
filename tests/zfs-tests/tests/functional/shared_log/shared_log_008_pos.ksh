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
# Copyright (c) 2024 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/shared_log/shared_log.kshlib

#
# DESCRIPTION:
#	Test zpool recycle
#
# STRATEGY:
#	1. Create shared log pool & clients
#	2. Verify zpool recycle -a doesn't recycle anything
#	3. Export clients
#	4. Verify zpool recycle -a recycles everything
#	5. Re-add clients and export both
#	6. Verify zpool recycle of a single client works as expected
#	7. Re-add client and export it
#	8. Verify zpool recycle of multiple clients works as expected
#

verify_runnable "global"

log_assert "Test zpool recycle."
log_onexit cleanup

typeset FS="$TESTPOOL/fs"

log_must create_pool $LOGPOOL -L "$DISK0"
log_must create_pool $TESTPOOL -l $LOGPOOL "$DISK1"
log_must create_pool ${TESTPOOL}2 -l $LOGPOOL "$DISK2"
log_must zfs create -o sync=always ${TESTPOOL}/fs
log_must zfs create -o sync=always ${TESTPOOL}2/fs
log_must dd if=/dev/urandom of=/${TESTPOOL}/fs/f1 bs=128k count=128
log_must dd if=/dev/urandom of=/${TESTPOOL}2/fs/f1 bs=128k count=128
log_must eval "zpool recycle -a -v $LOGPOOL | grep '\\[\\]' >/dev/null"

log_must zpool export $TESTPOOL
log_must zpool export ${TESTPOOL}2
log_must zpool recycle -a -v $LOGPOOL
log_mustnot zpool import $TESTPOOL
log_mustnot zpool import ${TESTPOOL}2

log_must zpool import -m -L $LOGPOOL $TESTPOOL
log_must zpool import -m -L $LOGPOOL ${TESTPOOL}2
log_must dd if=/dev/urandom of=/${TESTPOOL}/fs/f1 bs=128k count=128
log_must zpool export $TESTPOOL
log_must zpool export ${TESTPOOL}2
log_must zpool recycle $LOGPOOL $TESTPOOL
log_mustnot zpool import $TESTPOOL

log_must zpool import -m -L $LOGPOOL $TESTPOOL
log_must dd if=/dev/urandom of=/${TESTPOOL}/fs/f1 bs=128k count=128
log_must zpool export $TESTPOOL
log_must zpool recycle $LOGPOOL $TESTPOOL ${TESTPOOL2}
log_mustnot zpool import $TESTPOOL
log_mustnot zpool import ${TESTPOOL}2

log_pass "Test zpool recycle."
