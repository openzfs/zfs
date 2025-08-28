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
# 2. Create a user
# 3. Create an encrypted dataset
# 4. Write random data to the encrypted dataset
# 5. Snapshot the dataset
# 6. As root: attempt a send and raw send (both should succeed)
# 7. As user: attempt a send and raw send (both should fail, no permission)
# 8. Create a delegation (zfs allow -u user send testpool/encrypted_dataset)
# 9. As root: attempt a send and raw send (both should succeed)
# 10. As user: attempt a send and raw send (both should succeed)
# 11. Create a delegation (zfs allow -u user sendraw testpool/encrypted_dataset)
# 12. As root: attempt a send and raw send (both should succeed)
# 13. As user: attempt a send and raw send (send should fail, raw send should succeed)
# 14. Disable delegation (zfs unallow)
# 15. As root: attempt a send and raw send (both should succeed)
# 16. As user: attempt a send and raw send (both should fail, no permission)
# 17. Clean up (handled by framework)
# root tests to verify this doesnt affect root user under ../cli_root/zfs_send_delegation/
. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/zfs_create_common.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/properties.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib
. $STF_SUITE/tests/functional/delegate/delegate.cfg

# create encrypted dataset

log_must eval "echo $PASSPHRASE | zfs create -o encryption=on -o keyformat=passphrase $TESTPOOL/$TESTFS1"

# create target dataset for receives
log_must zfs create $TESTPOOL/$TESTFS2

# set user perms
# need to run chown for fs permissions for $OTHER1
typeset perms="snapshot,reservation,compression,checksum,userprop,receive,mount,create"

log_must zfs allow $OTHER1 $perms $TESTPOOL/$TESTFS1
log_must zfs allow $OTHER1 $perms $TESTPOOL/$TESTFS2
log_must chown ${OTHER1}:${OTHER_GROUP} /$TESTPOOL/$TESTFS2

# create random data
log_must fill_fs $TESTPOOL/$TESTFS1/child 1 2047 1024 1 R

# snapshot
log_must zfs snapshot $TESTPOOL/$TESTFS1@snap1

# note
# we need to use `sh -c` here becuase the quoting on <<<"$*" in the user_run wrapper is broken once pipes and redirects get involved

# check baseline send abilities (should fail)
log_mustnot user_run $OTHER1 sh -c "'zfs send $TESTPOOL/$TESTFS1@snap1 | zfs receive -u $TESTPOOL/$TESTFS2/zfsrecv0_user_datastream.$$'"
# verify nothing went through
if [ -s $TESTPOOL/$TESTFS2/zfsrecv0_user_datastream.$$ ]
then
	log_fail "A zfs recieve was completed in $TESTPOOL/$TESTFS2/zfsrecv0_user_datastream !"
fi
log_mustnot user_run $OTHER1 sh -c "'zfs send -w $TESTPOOL/$TESTFS1@snap1 | zfs receive -u $TESTPOOL/$TESTFS2/zfsrecv0raw_user_datastream.$$'"
# verify nothing went through
if [ -s $TESTPOOL/$TESTFS2/zfsrecv0raw_user_datastream.$$ ]
then
	log_fail "A zfs recieve was completed in $TESTPOOL/$TESTFS2/zfsrecv0raw_user_datastream !"
fi

# create delegation
log_must zfs allow $OTHER1 send $TESTPOOL/$TESTFS1

# attempt send with full allow (should pass)
log_must user_run $OTHER1 sh -c "'zfs send $TESTPOOL/$TESTFS1@snap1 | zfs receive -u $TESTPOOL/$TESTFS2/zfsrecv1_user_datastream.$$'"
log_must user_run $OTHER1 sh -c "'zfs send -w $TESTPOOL/$TESTFS1@snap1 | zfs receive -u $TESTPOOL/$TESTFS2/zfsrecv1raw_user_datastream.$$'"


# create raw delegation
log_must zfs allow $OTHER1 send:raw $TESTPOOL/$TESTFS1
# We have to remove 'send' to confirm 'send raw' only allows what we want
log_must zfs unallow -u $OTHER1 send $TESTPOOL/$TESTFS1

# test new sendraw abilities (send should fail, sendraw should pass)
log_mustnot user_run $OTHER1 sh -c "'zfs send $TESTPOOL/$TESTFS1@snap1 | zfs receive -u $TESTPOOL/$TESTFS2/zfsrecv2_user_datastream.$$'"
 verify nothing went through
if [ -s $TESTPOOL/$TESTFS2/zfsrecv2_user_datastream.$$ ]
then
	log_fail "A zfs recieve was completed in $TESTPOOL/$TESTFS2/zfsrecv2_user_datastream !"
fi
log_must user_run $OTHER1 sh -c "'zfs send -w $TESTPOOL/$TESTFS1@snap1 | zfs receive -u $TESTPOOL/$TESTFS2/zfsrecv2raw_user_datastream.$$'"

# disable raw delegation
log_must zfs unallow -u $OTHER1 send:raw $TESTPOOL/$TESTFS1
log_must zfs allow $OTHER1 send $TESTPOOL/$TESTFS1

# test with raw taken away (should pass)
log_must user_run $OTHER1 sh -c "'zfs send $TESTPOOL/$TESTFS1@snap1 | zfs receive -u $TESTPOOL/$TESTFS2/zfsrecv3_user_datastream.$$'"
log_must user_run $OTHER1 sh -c "'zfs send -w $TESTPOOL/$TESTFS1@snap1 | zfs receive -u $TESTPOOL/$TESTFS2/zfsrecv3raw_user_datastream.$$'"

# disable send abilities
log_must zfs unallow -u $OTHER1 send $TESTPOOL/$TESTFS1

# verify original send abilities (should fail)
log_mustnot user_run $OTHER1 sh -c "'zfs send $TESTPOOL/$TESTFS1@snap1 | zfs receive -u $TESTPOOL/$TESTFS2/zfsrecv4_user_datastream.$$'"
 verify nothing went through
if [ -s $TESTPOOL/$TESTFS2/zfsrecv4_user_datastream.$$ ]
then
        log_fail "A zfs recieve was completed in $TESTPOOL/$TESTFS2/zfsrecv4_user_datastream !"
fi
log_mustnot user_run $OTHER1 sh -c "'zfs send -w $TESTPOOL/$TESTFS1@snap1 | zfs receive -u $TESTPOOL/$TESTFS2/zfsrecv4raw_user_datastream.$$'"
 verify nothing went through
if [ -s $TESTPOOL/$TESTFS2/zfsrecv4raw_user_datastream.$$ ]
then
        log_fail "A zfs recieve was completed in $TESTPOOL/$TESTFS2/zfsrecv4raw_user_datastream !"
fi


function cleanup
{
    datasetexists $TESTPOOL/$TESTFS1 && \
	    destroy_dataset $TESTPOOL/$TESTFS1 -r \
            destroy_dataset $TESTPOOL/$TESTFS2 -r

}
