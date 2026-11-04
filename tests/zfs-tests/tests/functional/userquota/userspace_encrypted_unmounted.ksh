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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
# 'zfs userspace', 'zfs groupspace', 'zfs projectspace' and setting a user
# quota work on an encrypted filesystem that isn't mounted but has its key
# loaded.
#
# STRATEGY:
# 1. Create an encrypted filesystem with a file owned by a user
# 2. Export and import the pool, so that nothing of the filesystem is cached,
#    and load its key without mounting it
# 3. Check that the space reports and a user quota work
#

function cleanup
{
	destroy_pool $POOLNAME
	rm -rf $FILEDIR
}

log_onexit cleanup

FILEDIR="$TEST_BASE_DIR/userspace_encrypted_unmounted"
POOLNAME="testpool$$"
ENC_FS="$POOLNAME/encfs"

log_assert "Space reports and user quotas work on an unmounted encrypted" \
	"filesystem with its key loaded"

# 1.
log_must mkdir -p $FILEDIR
log_must truncate -s $SPA_MINDEVSIZE $FILEDIR/dev
log_must zpool create $POOLNAME $FILEDIR/dev
log_must eval "echo 'password' | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $ENC_FS"
mkmount_writable $ENC_FS
log_must user_run $QUSER1 mkfile 1m $(get_prop mountpoint $ENC_FS)/file

# 2.
log_must zpool export $POOLNAME
log_must zpool import -d $FILEDIR $POOLNAME
log_must eval "echo 'password' | zfs load-key $ENC_FS"
log_must eval "[[ $(get_prop mounted $ENC_FS) == no ]]"

# 3.
log_must eval "zfs userspace -H $ENC_FS | grep -q $QUSER1"
log_must zfs groupspace $ENC_FS
log_must zfs projectspace $ENC_FS
log_must zfs set userquota@$QUSER1=50m $ENC_FS
quota=$(get_prop userquota@$QUSER1 $ENC_FS)
log_must eval "[[ $quota == $((50 * 1024 * 1024)) ]]"

log_pass "Space reports and user quotas work on an unmounted encrypted" \
	"filesystem with its key loaded"
