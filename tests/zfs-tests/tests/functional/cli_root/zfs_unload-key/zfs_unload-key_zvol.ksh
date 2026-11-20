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
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
#	'unload-key' of a dataset with an open zvol under it must fail
#	with EBUSY rather than parking the pool's zvol taskq behind the
#	open device.
#
# STRATEGY:
#	1. Create an encrypted dataset with a child that has its own
#	   encryption root and a zvol under the child.
#	2. Unmount the child and hold the zvol device open.
#	3. 'zfs unload-key' of the parent must fail with EBUSY.
#	4. A 'zfs create -V' on the same pool must complete promptly.
#	5. Release the holder; the unload must now succeed.
#

verify_runnable "both"

function cleanup
{
	if [[ -n $create_pid ]]; then
		kill $create_pid 2>/dev/null
		wait $create_pid 2>/dev/null
	fi
	exec 9<&- 2>/dev/null
	datasetexists $TESTPOOL/vol2 && destroy_dataset $TESTPOOL/vol2
	datasetexists $TESTPOOL/$TESTFS1 && destroy_dataset $TESTPOOL/$TESTFS1 -r
}

fs=$TESTPOOL/$TESTFS1
child=$fs/crypt
zvol=$child/zv
zvoldev=${ZVOL_DEVDIR}/$zvol
create_pid=""

log_assert "Unload-key of a dataset with an open zvol under it returns EBUSY"
log_onexit cleanup

# 1. Encrypted parent, child with its own encryption root, zvol under child
log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $fs"
log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $child"
log_must zfs create -V 64M $zvol
block_device_wait $zvoldev

# 2. Unmount the child and hold the zvol device open.  A mounted child
#    would hold a reference to its own key and mask the bug.
log_must zfs unmount $child
log_must eval "exec 9< $zvoldev"

# 3. Unload-key must fail with EBUSY
log_mustnot zfs unload-key $fs
log_must key_available $fs

# 4. A later zvol operation on the same pool must complete promptly
zfs create -V 64M $TESTPOOL/vol2 &
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

# 5. Release the holder; the unload must now succeed
log_must eval "exec 9<&-"
log_must zfs unload-key $fs
log_must key_unavailable $fs
log_must key_available $child

log_pass "Unload-key of a dataset with an open zvol under it returned EBUSY"
