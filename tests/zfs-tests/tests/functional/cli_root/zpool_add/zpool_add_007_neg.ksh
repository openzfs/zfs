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

#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_add/zpool_add.kshlib

#
# DESCRIPTION:
#       'zpool add' should return an error with badly-formed parameters,
#
# STRATEGY:
#	1. Create an array of parameters
#	2. For each parameter in the array, execute 'zpool add'
#	3. Verify an error is returned.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "'zpool add' should return an error with badly-formed parameters."

log_onexit cleanup

set -A args "" "-f" "-n" "-?" "-nf" "-fn" "-f -n" "--f" "-blah" \
	"-? $TESTPOOL $DISK1"

create_pool $TESTPOOL $DISK0
log_must poolexists $TESTPOOL

typeset -i i=0
while (( $i < ${#args[*]} )); do
	log_mustnot zpool add ${args[i]}
	((i = i + 1))
done

log_pass "'zpool add' badly formed parameters fail as expected."
