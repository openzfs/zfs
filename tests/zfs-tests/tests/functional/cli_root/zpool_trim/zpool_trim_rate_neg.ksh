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
# Copyright (c) 2019 by Tim Chase. All rights reserved.
# Copyright (c) 2019 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_trim/zpool_trim.kshlib

#
# DESCRIPTION:
#	A badly formed parameter passed to 'zpool trim -r' should
#	return an error.
#
# STRATEGY:
#	1. Create an array containing bad 'zpool trim -r' parameters.
#	2. For each element, execute the sub-command.
#	3. Verify it returns an error.
#

DISK1=${DISKS%% *}
DISK2="$(echo $DISKS | cut -d' ' -f2)"

verify_runnable "global"

set -A args "a" "--%" "10X" "yes" "-?" "z 99"

log_assert "Execute 'zpool trim -r' using invalid parameters."

log_must zpool create -f $TESTPOOL mirror $DISK1 $DISK2

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_mustnot zpool trim -r ${args[i]} $TESTPOOL
        ((i = i + 1))
done

log_pass "Invalid parameters to 'zpool trim -r' fail as expected."
