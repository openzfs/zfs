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

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# A send stream carries the longname feature flag only once the source
# holds a long name, so a stream from a dataset with longname=on and no
# long name yet is accepted by a pool where feature@longname is disabled.
# Verify that the received longname=on is refused there, so that no long
# name can be created and the feature is never activated.
#
# Strategy:
# 1. Send, with properties, a filesystem with longname=on and no long name.
# 2. Receive it into a pool with feature@longname disabled.
# 3. Verify that the received filesystem has longname=off.
# 4. Verify that a long name cannot be created and that the feature stays
#    disabled.
#

verify_runnable "both"

typeset sendfs=$POOL/longname_disabled
typeset recvfs=$POOL3/longname_disabled
typeset stream=$BACKDIR/stream
typeset longfname="file-$(printf 'a%.0s' {1..512})"

function cleanup
{
	datasetexists $sendfs && destroy_dataset $sendfs -r
	destroy_pool $POOL3
	log_must zpool create $POOL3 $DISK3
	rm -f $stream
}

log_assert "A received longname=on needs feature@longname."
log_onexit cleanup

log_must zfs create -o longname=on $sendfs
log_must touch $(get_prop mountpoint $sendfs)/shortname
log_must zfs snapshot $sendfs@snap
log_must eval "zfs send -p $sendfs@snap >$stream"
log_mustnot stream_has_features $stream longname

destroy_pool $POOL3
log_must zpool create -o feature@longname=disabled $POOL3 $DISK3
log_must eval "zfs recv $recvfs <$stream"

typeset value=$(get_prop longname $recvfs)
[[ "$value" == "off" ]] || \
    log_fail "$recvfs has longname=$value with feature@longname disabled"

log_mustnot touch $(get_prop mountpoint $recvfs)/$longfname
log_must zpool sync $POOL3
typeset state=$(get_pool_prop feature@longname $POOL3)
[[ "$state" == "disabled" ]] || \
    log_fail "feature@longname is $state on $POOL3 (expected disabled)"

log_pass "A received longname=on needs feature@longname."
