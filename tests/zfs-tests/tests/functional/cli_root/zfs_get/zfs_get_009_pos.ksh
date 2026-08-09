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

. $STF_SUITE/tests/functional/cli_root/zfs_get/zfs_get_common.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_get/zfs_get_list_d.kshlib

#
# DESCRIPTION:
#	'zfs get -d <n>' should get expected output.
#
# STRATEGY:
#	1. Create a multiple depth filesystem.
#	2. 'zfs get -d <n>' to get the output.
#	3. 'zfs get -r|grep' to get the expected output.
#	4. Compare the two outputs, they should be same.
#

verify_runnable "both"

if is_kmemleak; then
	log_unsupported "Test case runs slowly when kmemleak is enabled"
fi

log_assert "'zfs get -d <n>' should get expected output."
log_onexit depth_fs_cleanup

set -A all_props type used available creation volsize referenced \
	compressratio mounted origin recordsize quota reservation mountpoint \
	sharenfs checksum compression atime devices exec readonly setuid \
	snapdir aclinherit canmount primarycache secondarycache version \
	usedbychildren usedbydataset usedbyrefreservation usedbysnapshots \
	userquota@root groupquota@root userused@root groupused@root
if is_freebsd; then
	set -A all_props ${all_props[*]} jailed aclmode
else
	set -A all_props ${all_props[*]} zoned acltype
fi

depth_fs_setup

mntpnt=$(get_prop mountpoint $DEPTH_FS)
DEPTH_OUTPUT="$mntpnt/depth_output"
EXPECT_OUTPUT="$mntpnt/expect_output"
typeset -i prop_numb=16
typeset -i old_val=0
typeset -i j=0
typeset eg_opt="$DEPTH_FS"$
for dp in ${depth_array[@]}; do
	(( j=old_val+1 ))
	while (( j<=dp && j<=MAX_DEPTH )); do
		eg_opt="$eg_opt""|depth""$j"$
		(( j+=1 ))
	done
	for prop in $(gen_option_str "${all_props[*]}" "" "," $prop_numb); do
		log_must eval "zfs get -H -d $dp -o name $prop $DEPTH_FS > $DEPTH_OUTPUT"
		log_must eval "zfs get -rH -o name $prop $DEPTH_FS | grep -E '$eg_opt' > $EXPECT_OUTPUT"
		log_must diff $DEPTH_OUTPUT $EXPECT_OUTPUT
	done
	(( old_val=dp ))
done

# Ensure 'zfs get -t snapshot <dataset>' works as though -d 1 was specified
log_must eval "zfs get -H -t snapshot -o name creation $DEPTH_FS > $DEPTH_OUTPUT"
log_must eval "zfs get -H -t snapshot -d 1 -o name creation $DEPTH_FS > $EXPECT_OUTPUT"
log_must diff $DEPTH_OUTPUT $EXPECT_OUTPUT

# Ensure 'zfs get -t snap' works as a shorthand for 'zfs get -t snapshot'
log_must eval "zfs get -H -t snap -d 1 -o name creation $DEPTH_FS > $DEPTH_OUTPUT"
log_must eval "zfs get -H -t snapshot -d 1 -o name creation $DEPTH_FS > $EXPECT_OUTPUT"
log_must diff $DEPTH_OUTPUT $EXPECT_OUTPUT

# Ensure 'zfs get -t bookmark <dataset>' works as though -d 1 was specified
log_must eval "zfs get -H -t bookmark -o name creation $DEPTH_FS > $DEPTH_OUTPUT"
log_must eval "zfs get -H -t bookmark -d 1 -o name creation $DEPTH_FS > $EXPECT_OUTPUT"
log_must diff $DEPTH_OUTPUT $EXPECT_OUTPUT


log_pass "'zfs get -d <n>' should get expected output."

