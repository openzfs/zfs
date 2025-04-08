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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_split/zpool_split.cfg

#
# DESCRIPTION:
# 'zpool split' should only work with supported options and parameters.
#
# STRATEGY:
# 1. Verify every supported option is accepted
# 2. Verify other unsupported options raise an error
# 3. Verify we cannot split a pool if the destination already exists
#

verify_runnable "both"

function cleanup
{
	destroy_pool $TESTPOOL
	destroy_pool $TESTPOOL2
	rm -f $DEVICE1 $DEVICE2 $DEVICE3
}

function setup_mirror
{
	truncate -s $SPA_MINDEVSIZE $DEVICE1
	truncate -s $SPA_MINDEVSIZE $DEVICE2
	log_must zpool create -f $TESTPOOL mirror $DEVICE1 $DEVICE2
}

log_assert "'zpool split' should only work with supported options and parameters."
log_onexit cleanup

typeset goodopts=(
    "" "-g" "-L" "-n" "-P" "-o comment=ok" "-o ro -R /mnt" "-l -R /mnt" "-gLnP")
typeset badopts=(
    "-f" "-h" "-x" "-Pp" "-l" "-G" "-o" "-o ro" "-o comment" "-R" "-R dir" "=")

DEVICE1="$TEST_BASE_DIR/device-1"
DEVICE2="$TEST_BASE_DIR/device-2"
DEVICE3="$TEST_BASE_DIR/device-3"

# 1. Verify every supported option and/or parameter is accepted
for opt in "${goodopts[@]}"
do
	setup_mirror
	log_must zpool split $opt $TESTPOOL $TESTPOOL2
	cleanup
done

# 2. Verify other unsupported options and/or parameters raise an error
setup_mirror
for opt in "${badopts[@]}"
do
	log_mustnot zpool split $opt $TESTPOOL $TESTPOOL2
done
cleanup

# 3. Verify we cannot split a pool if the destination already exists
setup_mirror
truncate -s $SPA_MINDEVSIZE $DEVICE3
log_must zpool create $TESTPOOL2 $DEVICE3
log_mustnot zpool split $TESTPOOL $TESTPOOL2

log_pass "'zpool split' only works with supported options and parameters."
