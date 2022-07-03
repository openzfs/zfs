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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
# 'zpool create' should return an error with badly formed parameters.
#
# STRATEGY:
# 1. Create an array of parameters
# 2. For each parameter in the array, execute 'zpool create'
# 3. Verify an error is returned.
#

verify_runnable "global"

set -A args  "" "-?" "-n" "-f" "-nf" "-fn" "-f -n" "--f" "-e" "-s" \
	"-m" "-R" "-m -R" "-Rm" "-mR" "-m $TESTDIR $TESTPOOL" \
	"-R $TESTDIR $TESTPOOL" "-m nodir $TESTPOOL $DISK0" \
	"-R nodir $TESTPOOL $DISK0" "-m nodir -R nodir $TESTPOOL $DISK0" \
	"-R nodir -m nodir $TESTPOOL $DISK0" "-R $TESTDIR -m nodir $TESTPOOL $DISK0" \
	"-R nodir -m $TESTDIR $TESTPOOL $DISK0" \
	"-blah" "$TESTPOOL" "$TESTPOOL blah" "$TESTPOOL c?t0d0" \
	"$TESTPOOL c0txd0" "$TESTPOOL c0t0dx" "$TESTPOOL cxtxdx" \
	"$TESTPOOL mirror" "$TESTPOOL raidz" "$TESTPOOL mirror raidz" \
	"$TESTPOOL raidz1" "$TESTPOOL mirror raidz1" \
	"$TESTPOOL draid1" "$TESTPOOL mirror draid1" \
	"$TESTPOOL mirror c?t?d?" "$TESTPOOL mirror $DISK0 c0t1d?" \
	"$TESTPOOL RAIDZ $DISK0 $DISK1" \
	"$TESTPOOL $DISK0 log $DISK1 log $DISK2" \
	"$TESTPOOL $DISK0 spare $DISK1 spare $DISK2" \
	"$TESTPOOL RAIDZ1 $DISK0 $DISK1" "$TESTPOOL MIRROR $DISK0" \
	"$TESTPOOL DRAID $DISK1 $DISK2 $DISK3" "$TESTPOOL raidz $DISK0" \
	"$TESTPOOL raidz1 $DISK0" "$TESTPOOL draid $DISK0" \
	"$TESTPOOL draid2 $DISK0 $DISK1" \
	"$TESTPOOL draid $DISK0 $DISK1 $DISK2 spare s0-draid1-0" \
	"1tank $DISK0" "1234 $DISK0" "?tank $DISK0" \
	"tan%k $DISK0" "ta@# $DISK0" "tan+k $DISK0" \
	"$BYND_MAX_NAME $DISK0"

log_assert "'zpool create' should return an error with badly-formed parameters."
log_onexit default_cleanup_noexit

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_mustnot zpool create ${args[i]}
	((i = i + 1))
done

log_pass "'zpool create' with badly formed parameters failed as expected."
