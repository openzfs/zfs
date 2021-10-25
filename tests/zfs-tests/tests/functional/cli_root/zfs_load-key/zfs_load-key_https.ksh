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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
# 'zfs load-key' should load a dataset's key from an https:// URL,
# but fail to do so if the domain doesn't exist or the file 404s.
#
# STRATEGY:
# 1. Try to create a dataset pointing to an RFC6761-guaranteed unresolvable domain,
#    one to the sshd port (which will be either unoccupied (ECONNREFUSED)
#                          or have sshd on it ("wrong version number")).
#    and one pointing to an URL that will always 404.
# 2. Create encrypted datasets with keylocation=https://address
# 3. Unmount the datasets and unload their keys
# 4. Attempt to load the keys
# 5. Verify the keys are loaded
# 6. Attempt to mount the datasets
#

verify_runnable "both"

function cleanup
{
	for fs in "$TESTFS1" "$TESTFS2" "$TESTFS3"; do
		datasetexists $TESTPOOL/$fs && \
			destroy_dataset $TESTPOOL/$fs
	done
}
log_onexit cleanup

log_assert "'zfs load-key' should load a key from a file"

log_mustnot zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=https://invalid./where-ever $TESTPOOL/$TESTFS1

log_mustnot zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=https://$HTTPS_HOSTNAME:22 $TESTPOOL/$TESTFS1

log_mustnot zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=$(get_https_base_url)/ENOENT $TESTPOOL/$TESTFS1

log_must zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=$(get_https_base_url)/PASSPHRASE $TESTPOOL/$TESTFS1

log_must zfs create -o encryption=on -o keyformat=hex \
	-o keylocation=$(get_https_base_url)/HEXKEY $TESTPOOL/$TESTFS2

log_must zfs create -o encryption=on -o keyformat=raw \
	-o keylocation=$(get_https_base_url)/RAWKEY $TESTPOOL/$TESTFS3

for fs in "$TESTFS1" "$TESTFS2" "$TESTFS3"; do
	log_must zfs unmount $TESTPOOL/$fs
	log_must zfs unload-key $TESTPOOL/$fs
done
for fs in "$TESTFS1" "$TESTFS2" "$TESTFS3"; do
	log_must zfs load-key $TESTPOOL/$fs
	log_must key_available $TESTPOOL/$fs
	log_must zfs mount $TESTPOOL/$fs
done

log_pass "'zfs load-key' loads a key from a file"
