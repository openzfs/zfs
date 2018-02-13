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
# Copyright (c) 2017, Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/zfs_create_common.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/properties.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
# ZFS should create datasets only if they have a valid combination of
# encryption properties set.
#
# penc	= parent encrypted
# enc	= encryption
# loc	= keylocation provided
# fmt	= keyformat provided
#
# penc	enc	fmt	loc	valid	notes
# -------------------------------------------
# no	unspec	0	0	yes	inherit no encryption (not tested here)
# no	unspec	0	1	no	no crypt specified
# no	unspec	1	0	no	no crypt specified
# no	unspec	1	1	no	no crypt specified
# no	off	0	0	yes	explicit no encryption
# no	off	0	1	no	keylocation given, but crypt off
# no	off	1	0	no	keyformat given, but crypt off
# no	off	1	1	no	keyformat given, but crypt off
# no	on	0	0	no	no keyformat specified for new key
# no	on	0	1	no	no keyformat specified for new key
# no	on	1	0	yes	new encryption root
# no	on	1	1	yes	new encryption root
# yes	unspec	0	0	yes	inherit encryption
# yes	unspec	0	1	no	no keyformat specified
# yes	unspec	1	0	yes	new encryption root, crypt inherited
# yes	unspec	1	1	yes	new encryption root, crypt inherited
# yes	off	0	0	no	unencrypted child of encrypted parent
# yes	off	0	1	no	unencrypted child of encrypted parent
# yes	off	1	0	no	unencrypted child of encrypted parent
# yes	off	1	1	no	unencrypted child of encrypted parent
# yes	on	0	0	yes	inherited encryption, local crypt
# yes	on	0	1	no	no keyformat specified for new key
# yes	on	1	0	yes	new encryption root
# yes	on	1	1	yes	new encryption root
#
# STRATEGY:
# 1. Attempt to create a dataset using all combinations of encryption
#    properties
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		log_must zfs destroy -r $TESTPOOL/$TESTFS1
	datasetexists $TESTPOOL/$TESTFS2 && \
		log_must zfs destroy -r $TESTPOOL/$TESTFS2
}
log_onexit cleanup

log_assert "ZFS should create datasets only if they have a valid" \
	"combination of encryption properties set."

# Unencrypted parent
log_must zfs create $TESTPOOL/$TESTFS1
log_mustnot zfs create -o keyformat=passphrase $TESTPOOL/$TESTFS1/c1
log_mustnot zfs create -o keylocation=prompt $TESTPOOL/$TESTFS1/c1
log_mustnot zfs create -o keyformat=passphrase -o keylocation=prompt \
	$TESTPOOL/$TESTFS1/c1

log_must zfs create -o encryption=off $TESTPOOL/$TESTFS1/c1
log_mustnot zfs create -o encryption=off -o keylocation=prompt \
	$TESTPOOL/$TESTFS1/c2
log_mustnot zfs create -o encryption=off -o keyformat=passphrase \
	$TESTPOOL/$TESTFS1/c2
log_mustnot zfs create -o encryption=off -o keyformat=passphrase \
	-o keylocation=prompt $TESTPOOL/$TESTFS1/c2

log_mustnot zfs create -o encryption=on $TESTPOOL/$TESTFS1/c2
log_mustnot zfs create -o encryption=on -o keylocation=prompt \
	$TESTPOOL/$TESTFS1/c2
log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase $TESTPOOL/$TESTFS1/c3"
log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $TESTPOOL/$TESTFS1/c4"

# Encrypted parent
log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase $TESTPOOL/$TESTFS2"

log_must zfs create $TESTPOOL/$TESTFS2/c1
log_mustnot zfs create -o keylocation=prompt $TESTPOOL/$TESTFS2/c2
log_must eval "echo $PASSPHRASE | zfs create -o keyformat=passphrase" \
	"$TESTPOOL/$TESTFS2/c3"
log_must eval "echo $PASSPHRASE | zfs create -o keyformat=passphrase" \
	"-o keylocation=prompt $TESTPOOL/$TESTFS2/c4"

log_mustnot zfs create -o encryption=off $TESTPOOL/$TESTFS2/c5
log_mustnot zfs create -o encryption=off -o keylocation=prompt \
	$TESTPOOL/$TESTFS2/c5
log_mustnot zfs create -o encryption=off -o keyformat=passphrase \
	$TESTPOOL/$TESTFS2/c5
log_mustnot zfs create -o encryption=off -o keyformat=passphrase \
	-o keylocation=prompt $TESTPOOL/$TESTFS2/c5

log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"$TESTPOOL/$TESTFS2/c5"
log_mustnot zfs create -o encryption=on -o keylocation=prompt \
	$TESTPOOL/$TESTFS2/c6
log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase $TESTPOOL/$TESTFS2/c6"
log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $TESTPOOL/$TESTFS2/c7"

log_pass "ZFS creates datasets only if they have a valid combination of" \
	"encryption properties set."
