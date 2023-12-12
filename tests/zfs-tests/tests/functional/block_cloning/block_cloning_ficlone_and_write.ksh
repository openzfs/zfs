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
# Copyright (c) 2023, Kay Pedersen <mail@mkwg.de>
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

verify_runnable "global"

if [[ $(linux_version) -lt $(linux_version "4.5") ]]; then
  log_unsupported "copy_file_range not available before Linux 4.5"
fi

claim="O_TRUNC, writing and FICLONE to a large (>4G) file shouldn't fail"

log_assert $claim

NO_LOOP_BREAK=true

function cleanup
{
    datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

function loop
{
    while $NO_LOOP_BREAK; do clonefile -c -t -q /$TESTPOOL/file /$TESTPOOL/clone; done
}

log_onexit cleanup

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $DISKS

log_must dd if=/dev/urandom of=/$TESTPOOL/file bs=1M count=4000
log_must sync_pool $TESTPOOL


log_note "Copying entire file with FICLONE"

log_must clonefile -c /$TESTPOOL/file /$TESTPOOL/clone
log_must sync_pool $TESTPOOL

log_must have_same_content /$TESTPOOL/file /$TESTPOOL/clone

log_note "looping a clone"
loop &

log_must dd if=/dev/urandom of=/$TESTPOOL/clone bs=1M count=4000
log_must dd if=/dev/urandom of=/$TESTPOOL/clone bs=1M count=4000

NO_LOOP_BREAK=false

# just to be sure all background jobs are killed.
log_must kill $(jobs -p)

log_must sync_pool $TESTPOOL

log_pass $claim
