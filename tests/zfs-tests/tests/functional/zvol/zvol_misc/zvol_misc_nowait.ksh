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

. $STF_SUITE/tests/functional/zvol/zvol_misc/zvol_misc_common.kshlib

#
# DESCRIPTION:
#	Non-blocking (RWF_NOWAIT) I/O works on a blk-mq zvol with every
#	setting, including those that process requests in the submitting task.
#
# STRATEGY:
# 1. Create a blk-mq zvol with the default settings, and one with
#    volthreading=off.
# 2. On each, an O_DIRECT RWF_NOWAIT write succeeds, and an RWF_NOWAIT read
#    of the same block returns its data.
# 3. Set zvol_request_sync=1 and create a third zvol: the same on all three.
#

verify_runnable "global"

if ! is_linux; then
	log_unsupported "Only Linux has blk-mq zvols"
fi

typeset vol1=$TESTPOOL/nowait1
typeset vol2=$TESTPOOL/nowait2
typeset vol3=$TESTPOOL/nowait3
typeset -i failed=0

function cleanup
{
	for vol in $vol1 $vol2 $vol3; do
		datasetexists "$vol" && destroy_dataset "$vol"
	done
	log_must restore_tunable VOL_USE_BLK_MQ
	log_must restore_tunable VOL_REQUEST_SYNC
}

#
# A write, then a read of the same block, at an offset where udev does not
# look for signatures.
#
function check_nowait # volume setting
{
	typeset out

	out=$(nowait_io "$1" w33554432 r33554432)

	log_note "$2: $1: $(echo "$out" | xargs)"
	if [[ "$(echo "$out" | awk '{ print $2 }' | xargs)" != "ok ok" ]]; then
		((failed += 1))
	fi
}

log_assert "RWF_NOWAIT I/O works on a blk-mq zvol with every setting"
log_onexit cleanup

log_must save_tunable VOL_USE_BLK_MQ
log_must save_tunable VOL_REQUEST_SYNC
set_blk_mq 1
set_zvol_sync 0

log_must zfs create -V 64M "$vol1"
log_must zfs create -V 64M -o volthreading=off "$vol2"
check_nowait "$vol1" defaults
check_nowait "$vol2" volthreading=off

set_zvol_sync 1
log_must zfs create -V 64M "$vol3"
check_nowait "$vol1" zvol_request_sync=1
check_nowait "$vol3" zvol_request_sync=1

if ((failed > 0)); then
	log_fail "RWF_NOWAIT I/O failed in $failed of 4 checks"
fi

log_pass "RWF_NOWAIT I/O works on a blk-mq zvol with every setting"
