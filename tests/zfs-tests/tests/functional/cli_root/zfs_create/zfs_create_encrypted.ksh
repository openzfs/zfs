#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016, Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/zfs_create_common.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/properties.kshlib

#
# DESCRIPTION:
# 'zfs create' should be able to create an encrypted dataset with
# a valid encryption algorithm, keysource, and key.
#
# STRATEGY:
# 1. Create a filesystem for each encryption type
# 2. Create a filesystem for each keysource type
# 3. Verify that each filesystem has the correct properties set
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		log_must $ZFS destroy -f $TESTPOOL/$TESTFS1
}

log_onexit cleanup

set -A ENCRYPTION_ALGS "encryption=on" \
	"encryption=aes-128-ccm" \
	"encryption=aes-192-ccm" \
	"encryption=aes-256-ccm" \
	"encryption=aes-128-gcm" \
	"encryption=aes-192-gcm" \
	"encryption=aes-256-gcm"

set -A ENCRYPTION_PROPS "encryption=aes-256-ccm" \
	"encryption=aes-128-ccm" \
	"encryption=aes-192-ccm" \
	"encryption=aes-256-ccm" \
	"encryption=aes-128-gcm" \
	"encryption=aes-192-gcm" \
	"encryption=aes-256-gcm"

set -A KEYSOURCE_TYPES "keysource=raw,prompt" \
	"keysource=hex,prompt" \
	"keysource=passphrase,prompt"

set -A KEYSOURCES "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz" \
	"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" \
	"abcdefgh"

log_assert "'zfs create' should properly create encrypted datasets"

typeset -i i=0
while (( $i < ${#ENCRYPTION_ALGS[*]} )); do
	log_must eval 'echo ${KEYSOURCES[0]} | \
		$ZFS create -o ${ENCRYPTION_ALGS[$i]} -o ${KEYSOURCE_TYPES[0]} \
		$TESTPOOL/$TESTFS1'

	datasetexists $TESTPOOL/$TESTFS1 || \
		log_fail "zfs create -o ${ENCRYPTION_ALGS[$i]} \
		-o ${KEYSOURCE_TYPES[0]} $TESTPOOL/$TESTFS1 fail."

	propertycheck $TESTPOOL/$TESTFS1 ${ENCRYPTION_PROPS[i]} || \
		log_fail "${ENCRYPTION_ALGS[i]} is failed to set."
	propertycheck $TESTPOOL/$TESTFS1 ${KEYSOURCE_TYPES[0]} || \
		log_fail "${KEYSOURCE_TYPES[0]} is failed to set."

	log_must $ZFS destroy -f $TESTPOOL/$TESTFS1
	(( i = i + 1 ))
done

typeset -i j=0
while (( $j < ${#KEYSOURCE_TYPES[*]} )); do
	log_must eval 'echo ${KEYSOURCES[$j]} | \
		$ZFS create -o ${ENCRYPTION_ALGS[0]} -o ${KEYSOURCE_TYPES[$j]} \
		$TESTPOOL/$TESTFS1'

	datasetexists $TESTPOOL/$TESTFS1 || \
		log_fail "zfs create -o ${ENCRYPTION_ALGS[0]} \
		-o ${KEYSOURCE_TYPES[$j]} $TESTPOOL/$TESTFS1 fail."

	propertycheck $TESTPOOL/$TESTFS1 ${ENCRYPTION_PROPS[0]} || \
		log_fail "${ENCRYPTION_ALGS[0]} is failed to set."
	propertycheck $TESTPOOL/$TESTFS1 ${KEYSOURCE_TYPES[j]} || \
		log_fail "${KEYSOURCE_TYPES[j]} is failed to set."

	log_must $ZFS destroy -f $TESTPOOL/$TESTFS1
	(( j = j + 1 ))
done

log_pass "'zfs create properly creates encrypted datasets"
