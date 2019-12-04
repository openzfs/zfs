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
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
# 'zpool create' should create an encrypted dataset only if it has a valid
# combination of encryption properties set.
#
# enc	= encryption
# loc	= keylocation provided
# fmt	= keyformat provided
#
# U = unspecified
# N = off
# Y = on
#
# enc	fmt	loc	valid	notes
# -------------------------------------------
# U	0	1	no	no crypt specified
# U	1	0	no	no crypt specified
# U	1	1	no	no crypt specified
# N	0	0	yes	explicit no encryption
# N	0	1	no	keylocation given, but crypt off
# N	1	0	no	keyformat given, but crypt off
# N	1	1	no	keyformat given, but crypt off
# Y	0	0	no	no keyformat specified for new key
# Y	0	1	no	no keyformat specified for new key
# Y	1	1	no	unsupported combination of non-encryption props
# Y	1	0	yes	new encryption root
# Y	1	1	yes	new encryption root
#
# STRATEGY:
# 1. Attempt to create a dataset using all combinations of encryption
#    properties
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}
log_onexit cleanup

log_assert "'zpool create' should create an encrypted dataset only if it" \
	"has a valid combination of encryption properties set."

log_mustnot zpool create -O keylocation=prompt $TESTPOOL $DISKS
log_mustnot zpool create -O keyformat=passphrase $TESTPOOL $DISKS
log_mustnot zpool create -O keyformat=passphrase -O keylocation=prompt \
	$TESTPOOL $DISKS

log_must zpool create -O encryption=off $TESTPOOL $DISKS
log_must zpool destroy $TESTPOOL

log_mustnot zpool create -O encryption=off -O keylocation=prompt \
	$TESTPOOL $DISKS
log_mustnot zpool create -O encryption=off -O keyformat=passphrase \
	$TESTPOOL $DISKS
log_mustnot zpool create -O encryption=off -O keyformat=passphrase \
	-O keylocation=prompt $TESTPOOL $DISKS

log_mustnot zpool create -O encryption=on $TESTPOOL $DISKS
log_mustnot zpool create -O encryption=on -O keylocation=prompt \
	$TESTPOOL $DISKS

log_mustnot eval "echo $PASSPHRASE | zpool create -O encryption=on" \
	"-O keyformat=passphrase -O keylocation=prompt" \
	"-o feature@lz4_compress=disabled -O compression=lz4 $TESTPOOL $DISKS"

log_must eval "echo $PASSPHRASE | zpool create -O encryption=on" \
	"-O keyformat=passphrase $TESTPOOL $DISKS"
log_must zpool destroy $TESTPOOL

log_must eval "echo $PASSPHRASE | zpool create -O encryption=on" \
	"-O keyformat=passphrase -O keylocation=prompt $TESTPOOL $DISKS"
log_must zpool destroy $TESTPOOL

log_pass "'zpool create' creates an encrypted dataset only if it has a" \
	"valid combination of encryption properties set."
