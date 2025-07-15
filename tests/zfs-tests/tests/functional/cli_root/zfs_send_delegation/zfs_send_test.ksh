#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2025, Klara Inc.
#

# STRATEGY:
# 1. Create a pool (this is done by the test framework)
# 2. Create an encrypted dataset
# 3. Write random data to the encrypted dataset
# 4. Snapshot the dataset
# 5. As root: attempt a send and raw send (both should succeed)
# 6. Create a delegation (zfs allow -u user send testpool/encrypted_dataset)
# 7. As root: attempt a send and raw send (both should succeed)
# 8. Create a delegation (zfs allow -u user send:raw testpool/encrypted_dataset)
# 9. As root: attempt a send and raw send (both should succeed)
# 10. Disable delegation (zfs unallow)
# 11. As root: attempt a send and raw send (both should succeed)
# 12. Clean up (handled by framework)
#
# Tested as a user under ../cli_user/zfs_send_delegation_user/

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/zfs_create_common.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/properties.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib
. $STF_SUITE/tests/functional/delegate/delegate.cfg

# create encrypted dataset

log_must eval "echo $PASSPHRASE | zfs create -o encryption=on -o keyformat=passphrase $TESTPOOL/$TESTFS1"

# create target dataset for receives
if ! zfs list | grep testfs2 >/dev/null 2>&1; then
    dataset_created="TRUE"
    log_must zfs create $TESTPOOL/$TESTFS2
fi

# create user and group
typeset perms="snapshot,reservation,compression,checksum,userprop,receive"

log_note "Added permissions to the $OTHER1 user."
log_must zfs allow $OTHER1 $perms $TESTPOOL/$TESTFS1
log_must zfs allow $OTHER1 $perms $TESTPOOL/$TESTFS2

# create random data
log_must fill_fs $TESTPOOL/$TESTFS1/child 1 2047 1024 1 R

# snapshot
log_must zfs snapshot $TESTPOOL/$TESTFS1@snap1


# check baseline send abilities (should pass)
log_must eval "zfs send $TESTPOOL/$TESTFS1@snap1 | zfs receive $TESTPOOL/$TESTFS2/zfsrecv0_datastream.$$"
log_must eval "zfs send -w $TESTPOOL/$TESTFS1@snap1 | zfs receive $TESTPOOL/$TESTFS2/zfsrecv0raw_datastream.$$"


# create delegation
log_must zfs allow $OTHER1 send $TESTPOOL/$TESTFS1

# attempt send with full allow

log_must eval "zfs send $TESTPOOL/$TESTFS1@snap1 | zfs receive $TESTPOOL/$TESTFS2/zfsrecv1_datastream.$$"
log_must eval "zfs send -w $TESTPOOL/$TESTFS1@snap1 | zfs receive $TESTPOOL/$TESTFS2/zfsrecv1raw_datastream.$$"

# create raw delegation
log_must zfs allow $OTHER1 send:raw $TESTPOOL/$TESTFS1
log_must zfs unallow $OTHER1 send $TESTPOOL/$TESTFS1

# test new send abilities (should pass)
log_must eval "zfs send $TESTPOOL/$TESTFS1@snap1 | zfs receive $TESTPOOL/$TESTFS2/zfsrecv2_datastream.$$"
log_must eval "zfs send -w $TESTPOOL/$TESTFS1@snap1 | zfs receive $TESTPOOL/$TESTFS2/zfsrecv2raw_datastream.$$"


# disable raw delegation
zfs unallow $OTHER1 send:raw $TESTPOOL/$TESTFS1
zfs allow $OTHER1 send $TESTPOOL/$TESTFS1

# verify original send abilities (should pass)
log_must eval "zfs send $TESTPOOL/$TESTFS1@snap1 | zfs receive $TESTPOOL/$TESTFS2/zfsrecv3_datastream.$$"
log_must eval "zfs send -w $TESTPOOL/$TESTFS1@snap1 | zfs receive $TESTPOOL/$TESTFS2/zfsrecv3raw_datastream.$$"


function cleanup
{
    datasetexists $TESTPOOL/$TESTFS1 && \
	    destroy_dataset $TESTPOOL/$TESTFS1 -r \
            destroy_dataset $TESTPOOL/$TESTFS2 -r

}
