#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2017 Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
# 'zfs change-key -o' should change the pbkdf2 iterations.
#
# STRATEGY:
# 1. Create an encryption dataset with 200k PBKDF2 iterations
# 2. Unmount the dataset
# 3. Change the PBKDF2 iterations to 150k
# 4. Verify the PBKDF2 iterations
# 5. Unload the dataset's key
# 6. Attempt to load the dataset's key
#

verify_runnable "both"

function verify_pbkdf2iters
{
	typeset ds=$1
	typeset iterations=$2
	typeset iters=$(get_prop pbkdf2iters $ds)

	if [[ "$iters" != "$iterations" ]]; then
		log_fail "Expected $iterations iterations, got $iters"
	fi

	return 0
}

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1 -f
}
log_onexit cleanup

log_assert "'zfs change-key -o' should change the pbkdf2 iterations"

log_must eval "echo $PASSPHRASE > /$TESTPOOL/pkey"
log_must zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=file:///$TESTPOOL/pkey -o pbkdf2iters=200000 \
	$TESTPOOL/$TESTFS1

log_must zfs unmount $TESTPOOL/$TESTFS1
log_must verify_pbkdf2iters $TESTPOOL/$TESTFS1 "200000"

log_must zfs change-key -o pbkdf2iters=150000 $TESTPOOL/$TESTFS1
log_must verify_pbkdf2iters $TESTPOOL/$TESTFS1 "150000"

log_must zfs unload-key $TESTPOOL/$TESTFS1
log_must zfs load-key $TESTPOOL/$TESTFS1

log_pass "'zfs change-key -o' changes the pbkdf2 iterations"
