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
# Copyright (c) 2026 by HeonJe Lee. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/blkdev.shlib

#
# DESCRIPTION:
#	Destroying a snapshot whose snapdev=visible zvol device is open
#	must fail with EBUSY without blocking later zvol operations on
#	the pool.
#
# STRATEGY:
#	1. Create a zvol with a snapshot and snapdev=visible.
#	2. Hold the snapshot device open.
#	3. 'zfs destroy' must fail with EBUSY and the still-existing
#	   snapshot device must remain usable.
#	4. A 'zfs create -V' on the same pool must complete promptly.
#	5. Release the holder; the destroy must now succeed.
#

verify_runnable "global"

function cleanup
{
	if [[ -n $create_pid ]]; then
		kill $create_pid 2>/dev/null
		wait $create_pid 2>/dev/null
	fi
	exec 9<&- 2>/dev/null
	datasetexists $vol2 && destroy_dataset $vol2 -f
	datasetexists $vol && destroy_dataset $vol -r
}

vol=$TESTPOOL/vol
snap=$vol@snap
snapdev=${ZVOL_DEVDIR}/$vol@snap
vol2=$TESTPOOL/vol2
create_pid=""

log_assert "Destroying an open snapshot zvol returns EBUSY without \
    stalling later zvol operations"
log_onexit cleanup

# 1. zvol + snapshot + snapdev=visible
log_must zfs create -V 64M $vol
log_must zfs snapshot $snap
log_must zfs set snapdev=visible $vol
block_device_wait $snapdev

# 2. Hold the snapshot device open
log_must eval "exec 9< $snapdev"

# 3. Destroy must fail with EBUSY and leave the device usable
log_mustnot zfs destroy $snap
log_must dd if=$snapdev of=/dev/null bs=4k count=1

# 4. A later zvol operation on the same pool must complete promptly
zfs create -V 64M $vol2 &
create_pid=$!
typeset -i i=0
while ((i < 60)) && kill -0 $create_pid 2>/dev/null; do
	sleep 1
	((i += 1))
done
if kill -0 $create_pid 2>/dev/null; then
	log_fail "'zfs create -V' did not complete within 60s"
fi
wait $create_pid || log_fail "'zfs create -V' failed"

# 5. Release the holder; the destroy must now succeed
log_must eval "exec 9<&-"
log_must zfs destroy $snap
log_mustnot datasetexists $snap

log_pass "Destroy of an open snapshot zvol returned EBUSY without \
    stalling later zvol operations"
