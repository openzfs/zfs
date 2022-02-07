#!/bin/ksh -p
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2017 Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# ZFS should receive to an encrypted child dataset.
#
# STRATEGY:
# 1. Snapshot the default dataset
# 2. Create an encrypted dataset
# 3. Attempt to receive a stream to an encrypted child
# 4. Attempt to receive a stream with properties to an encrypted child
# 5. Attempt to receive a replication stream to an encrypted child
# 6. Unmount and unload the encrypted dataset keys
# 7. Attempt to receive a snapshot stream to an encrypted child
#

verify_runnable "both"

function cleanup
{
	snapexists $snap && destroy_dataset $snap -f

	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1 -r
}

log_onexit cleanup

log_assert "ZFS should receive encrypted filesystems into child dataset"

typeset passphrase="password"
typeset snap="$TESTPOOL/$TESTFS@snap"
typeset testfile="testfile"

log_must zfs snapshot $snap

log_must eval "echo $passphrase | zfs create -o encryption=on" \
	"-o keyformat=passphrase $TESTPOOL/$TESTFS1"

log_note "Verifying ZFS will receive to an encrypted child"
log_must eval "zfs send $snap | zfs receive $TESTPOOL/$TESTFS1/c1"
log_must test "$(get_prop 'encryption' $TESTPOOL/$TESTFS1/c1)" != "off"

# Unload the key, the following tests won't require it and we will test
# the receive checks as well.
log_must zfs unmount $TESTPOOL/$TESTFS1
log_must zfs unload-key $TESTPOOL/$TESTFS1

log_note "Verifying 'send -p' will receive to an unencrypted child"
log_must eval "zfs send -p $snap | zfs receive $TESTPOOL/$TESTFS1/c2"
log_must test "$(get_prop 'encryption' $TESTPOOL/$TESTFS1/c2)" == "off"

# For completeness add the property override case.
log_note "Verifying recv -o encyption=off' will receive to an unencrypted child"
log_must eval "zfs send $snap | \
	zfs receive -o encryption=off $TESTPOOL/$TESTFS1/c2o"
log_must test "$(get_prop 'encryption' $TESTPOOL/$TESTFS1/c2o)" == "off"

log_note "Verifying 'send -R' will receive to an unencrypted child"
log_must eval "zfs send -R $snap | zfs receive $TESTPOOL/$TESTFS1/c3"
log_must test "$(get_prop 'encryption' $TESTPOOL/$TESTFS1/c3)" == "off"

log_note "Verifying ZFS will not receive to an encrypted child when the" \
	"parent key is unloaded"
log_mustnot eval "zfs send $snap | zfs receive $TESTPOOL/$TESTFS1/c4"

log_pass "ZFS can receive encrypted filesystems into child dataset"
