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
# Copyright (c) 2019, DilOS
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/zfs_create_common.kshlib

#
# DESCRIPTION:
# 'zpool create' should create encrypted pools when using a valid encryption
# algorithm, key format, key location, and key.
#
# STRATEGY:
# 1. Create a pool for each combination of encryption type and key format
# 2. Verify that each filesystem has the correct properties set
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}
log_onexit cleanup

set -A ENCRYPTION_ALGS "encryption=on" \
	"encryption=aes-128-ccm" \
	"encryption=aes-192-ccm" \
	"encryption=aes-256-ccm" \
	"encryption=aes-128-gcm" \
	"encryption=aes-192-gcm" \
	"encryption=aes-256-gcm"

set -A ENCRYPTION_PROPS "encryption=aes-256-gcm" \
	"encryption=aes-128-ccm" \
	"encryption=aes-192-ccm" \
	"encryption=aes-256-ccm" \
	"encryption=aes-128-gcm" \
	"encryption=aes-192-gcm" \
	"encryption=aes-256-gcm"

set -A KEYFORMATS "keyformat=raw" \
	"keyformat=hex" \
	"keyformat=passphrase"

set -A USER_KEYS "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz" \
	"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" \
	"abcdefgh"

log_assert "'zpool create' should create encrypted pools when using a valid" \
	"encryption algorithm, key format, key location, and key."

typeset -i i=0
while (( i < ${#ENCRYPTION_ALGS[*]} )); do
	typeset -i j=0
	while (( j < ${#KEYFORMATS[*]} )); do
		log_must eval "printf '%s' ${USER_KEYS[j]} | zpool create" \
		"-O ${ENCRYPTION_ALGS[i]} -O ${KEYFORMATS[j]}" \
		"$TESTPOOL $DISKS"

		propertycheck $TESTPOOL ${ENCRYPTION_PROPS[i]} || \
			log_fail "failed to set ${ENCRYPTION_ALGS[i]}"
		propertycheck $TESTPOOL ${KEYFORMATS[j]} || \
			log_fail "failed to set ${KEYFORMATS[j]}"

		log_must zpool destroy $TESTPOOL
		(( j = j + 1 ))
	done
	(( i = i + 1 ))
done

log_pass "'zpool create' creates encrypted pools when using a valid" \
	"encryption algorithm, key format, key location, and key."
