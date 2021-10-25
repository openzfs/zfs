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
# Copyright (c) 2016 by Lawrence Livermore National Security, LLC.
# Use is subject to license terms.
#

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Run xattrtest on a dataset with large dnodes and xattr=sa
# to stress xattr usage of the extra bonus space and verify
# contents
#

TEST_FS=$TESTPOOL/large_dnode

verify_runnable "both"

function cleanup
{
	datasetexists $TEST_FS && destroy_dataset $TEST_FS
}

log_onexit cleanup
log_assert "xattrtest runs cleanly on dataset with large dnodes"

log_must zfs create $TEST_FS

set -A xattr_sizes "512" "1536" "3584" "7680" "15872"
set -A prop_values "1k"  "2k"   "4k"   "8k"   "16k"

for ((i=0; i < ${#prop_values[*]}; i++)) ; do
	prop_val=${prop_values[$i]}
	dir=/$TEST_FS/$prop_val
	xattr_size=${xattr_sizes[$i]}
	log_must zfs set dnsize=$prop_val $TEST_FS
	log_must mkdir $dir
	log_must xattrtest -R -y -s $xattr_size -f 1024 -p $dir
done

log_pass
