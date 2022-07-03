#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
#       'zpool add' should return an error with nonexistent pools or vdevs
#
# STRATEGY:
#	1. Create an array of parameters which contains nonexistent pools/vdevs
#	2. For each parameter in the array, execute 'zpool add'
#	3. Verify an error is returned
#

verify_runnable "global"

function cleanup
{
        poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "'zpool add' should return an error with nonexistent pools and vdevs"

log_onexit cleanup

set -A args "" "-f nonexistent_pool $DISK1" \
	"-f $TESTPOOL nonexistent_vdev"

create_pool $TESTPOOL $DISK0
log_must poolexists $TESTPOOL

typeset -i i=0
while (( $i < ${#args[*]} )); do
	log_mustnot zpool add ${args[i]}
	((i = i + 1))
done

log_pass "'zpool add' with nonexistent pools and vdevs fail as expected."
