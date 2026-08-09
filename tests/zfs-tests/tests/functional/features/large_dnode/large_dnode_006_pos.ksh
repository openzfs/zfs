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
