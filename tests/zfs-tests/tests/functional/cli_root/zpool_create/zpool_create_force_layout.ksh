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
# Copyright (c) 2023, Rob Norris <robn@despairlabs.com>
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
# 'zpool create <pool> <vspec> ...' should reject suboptimal pool layouts by
# default, but allow them if the --force=layout switch is offered.
#
# STRATEGY:
# 1. Try to create pools with suboptimal layouts, verify fail
# 2. Try again to create them with --force=layout, verify success
#

verify_runnable "global"

TMPDIR=${TMPDIR:-$TEST_BASE_DIR}

DISK1="$TMPDIR/dsk1"
DISK2="$TMPDIR/dsk2"
DISK3="$TMPDIR/dsk3"
DISK4="$TMPDIR/dsk4"
DISKS="$DISK1 $DISK2 $DISK3 $DISK4"

log_must mkfile $(($MINVDEVSIZE * 2)) $DISK1
log_must mkfile $(($MINVDEVSIZE * 2)) $DISK2
log_must mkfile $(($MINVDEVSIZE * 2)) $DISK3
log_must mkfile $(($MINVDEVSIZE * 2)) $DISK4

function cleanup
{
	default_cleanup_noexit
	log_must rm -f $DISKS
}

log_assert "'zpool create <pool> <vspec> ...' rejects suboptimal layouts," \
  " unless the --force=layout switch is provided."

log_onexit cleanup

function check_suboptimal
{
	typeset spec=$1
	log_mustnot zpool create $TESTPOOL $spec
	log_mustnot poolexists $TESTPOOL
	log_must zpool create --force=layout $TESTPOOL $spec
	log_must poolexists $TESTPOOL
	log_must destroy_pool $TESTPOOL
}

log_note "raidz1 with two drives is suboptimal"
log_must check_suboptimal "raidz1 $DISK1 $DISK2"

log_note "raidz2 with three drives is suboptimal"
log_must check_suboptimal "raidz2 $DISK1 $DISK2 $DISK3"

log_note "raidz3 with four drives is suboptimal"
log_must check_suboptimal "raidz3 $DISK1 $DISK2 $DISK3 $DISK4"

log_note "draid1 with two drives is suboptimal"
log_must check_suboptimal "draid1 $DISK1 $DISK2"

log_note "draid2 with three drives is suboptimal"
log_must check_suboptimal "draid2 $DISK1 $DISK2 $DISK3"

log_note "draid3 with four drives is suboptimal"
log_must check_suboptimal "draid3 $DISK1 $DISK2 $DISK3 $DISK4"

log_pass "'zpool create --force=layout ...' success."
