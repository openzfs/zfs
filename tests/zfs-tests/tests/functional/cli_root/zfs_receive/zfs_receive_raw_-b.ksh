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

#
# DESCRIPTION:
# 'zfs receive' must accept a 'zfs send -b -w -R' stream of a raw-received
# encryption root (openzfs/zfs#17011).
#
# STRATEGY:
# 1. Create an encrypted dataset and snapshot it.
# 2. Replicate it raw to a backup dataset, without loading its key.
# 3. Send the backup with 'zfs send -b -w -R', which carries only received
#    properties, and receive it into another dataset with 'zfs recv -e'.
# 4. The receive must succeed, and the restored dataset must be an
#    encryption root whose key loads with the original passphrase.
#

verify_runnable "both"

typeset src=$TESTPOOL/$TESTFS1
typeset backup=$TESTPOOL/$TESTFS2
typeset restore=$TESTPOOL/restore_-b
typeset passphrase="password"

function cleanup
{
	for ds in $restore $backup $src; do
		datasetexists $ds && destroy_dataset $ds -r
	done
}

log_onexit cleanup

log_assert "'zfs receive' accepts a 'send -b -w -R' stream of an encryption root"

log_must eval "echo $passphrase | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $src"
log_must zfs snapshot -r $src@snap
log_must eval "zfs send -w -R $src@snap | zfs receive -u $backup"

log_must zfs create $restore
log_must eval "zfs send -b -w -R $backup@snap | zfs receive -F -u -e $restore"

typeset name=$restore/$(basename $backup)
log_must datasetexists $name@snap
[[ "$(get_prop encryptionroot $name)" == "$name" ]] || \
	log_fail "$name is not its own encryption root"
log_must eval "echo $passphrase | zfs load-key $name"

log_pass "'zfs receive' accepts a 'send -b -w -R' stream of an encryption root"
