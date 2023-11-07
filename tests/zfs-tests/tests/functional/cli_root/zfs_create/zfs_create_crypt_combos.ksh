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
. $STF_SUITE/tests/functional/cli_root/zfs_create/properties.kshlib

#
# DESCRIPTION:
# 'zfs create' should create an encrypted dataset with a valid encryption
# algorithm, key format, key location, and key.
#
# STRATEGY:
# 1. Create a filesystem for each combination of encryption type and key format
# 2. Verify that each filesystem has the correct properties set
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1 -f
}

log_onexit cleanup

set -A ENCRYPTION_ALGS \
	"encryption=on" \
	"encryption=aes-128-ccm" \
	"encryption=aes-192-ccm" \
	"encryption=aes-256-ccm" \
	"encryption=aes-128-gcm" \
	"encryption=aes-192-gcm" \
	"encryption=aes-256-gcm" \
	"encryption=chacha20-poly1305"

set -A ENCRYPTION_PROPS \
	"encryption=aes-256-gcm" \
	"encryption=aes-128-ccm" \
	"encryption=aes-192-ccm" \
	"encryption=aes-256-ccm" \
	"encryption=aes-128-gcm" \
	"encryption=aes-192-gcm" \
	"encryption=aes-256-gcm" \
	"encryption=chacha20-poly1305"

set -A KEYFORMATS "keyformat=raw" \
	"keyformat=hex" \
	"keyformat=passphrase"

set -A USER_KEYS "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz" \
	"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" \
	"abcdefgh"

log_assert "'zfs create' should create encrypted datasets using all" \
	"combinations of supported properties"

typeset -i i=0
while (( i < ${#ENCRYPTION_ALGS[*]} )); do
	typeset -i j=0
	while (( j < ${#KEYFORMATS[*]} )); do
		log_must eval "printf '%s' ${USER_KEYS[j]} | zfs create" \
			"-o ${ENCRYPTION_ALGS[i]} -o ${KEYFORMATS[j]}" \
			"$TESTPOOL/$TESTFS1"

		datasetexists $TESTPOOL/$TESTFS1 || \
			log_fail "Failed to create dataset using" \
			"${ENCRYPTION_ALGS[i]} and ${KEYFORMATS[j]}"

		propertycheck $TESTPOOL/$TESTFS1 ${ENCRYPTION_PROPS[i]} || \
			log_fail "failed to set ${ENCRYPTION_ALGS[i]}"
		propertycheck $TESTPOOL/$TESTFS1 ${KEYFORMATS[j]} || \
			log_fail "failed to set ${KEYFORMATS[j]}"

		log_must_busy zfs destroy -f $TESTPOOL/$TESTFS1
		(( j = j + 1 ))
	done
	(( i = i + 1 ))
done

log_pass "'zfs create' creates encrypted datasets using all combinations of" \
	"supported properties"
