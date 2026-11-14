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
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
# 'zdb -d' reports the form of an encrypted filesystem's local MAC.
#
# STRATEGY:
# 1. Create an encrypted filesystem with the projectmac pool property set to
#    off and unload its key, then create another with it set to
#    on, and raw send a snapshot of the second
# 2. Check that zdb -d reports that the local MAC is set, without a key
# 3. Check that zdb -K -d reports the form of each
# 4. Check that zdb -d reports the raw-received dataset's MAC as zeroed
# 5. Check that zdb -d reports no local MAC for an unencrypted filesystem
#

verify_runnable "both"

NO_PROJECT=$TESTPOOL/noproject
PROJECT=$TESTPOOL/project
RECV=$TESTPOOL/recv

function cleanup
{
	default_cleanup_noexit
}

function create_encrypted # <dataset>
{
	log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	    "-o keyformat=passphrase $1"
	log_must eval "echo data > $(get_prop mountpoint $1)/file"
	sync_pool $TESTPOOL true
}

function check_local_mac # <expected> <zdb args>
{
	typeset expected=$1
	shift

	log_must eval "zdb $* | grep -q 'Local MAC: $expected'"
}

log_onexit cleanup

log_must default_setup_noexit $DISKS

log_assert "'zdb -d' reports the form of an encrypted filesystem's local MAC"

# 1.
log_must zpool set projectmac=off $TESTPOOL
create_encrypted $NO_PROJECT
log_must zfs unmount $NO_PROJECT
log_must zfs unload-key $NO_PROJECT
log_must zpool set projectmac=on $TESTPOOL
create_encrypted $PROJECT
log_must zfs snapshot $PROJECT@snap
log_must eval "zfs send -w $PROJECT@snap | zfs recv $RECV"
sync_pool $TESTPOOL true

# 2.
check_local_mac "set (load the key" -d $NO_PROJECT
check_local_mac "set (load the key" -d $PROJECT

# 3.
check_local_mac "without project accounting" -K $PASSPHRASE -d $NO_PROJECT
check_local_mac "with project accounting" -K $PASSPHRASE -d $PROJECT

# 4.
check_local_mac "zeroed" -d $RECV

# 5.
output=$(zdb -d $TESTPOOL/$TESTFS) ||
    log_fail "zdb -d $TESTPOOL/$TESTFS failed"
[[ $output != *"Local MAC"* ]] || \
    log_fail "zdb -d reports a local MAC for $TESTPOOL/$TESTFS"

log_pass "'zdb -d' reports the form of an encrypted filesystem's local MAC"
