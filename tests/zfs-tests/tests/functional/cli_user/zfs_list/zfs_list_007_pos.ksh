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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zfs_get/zfs_get_list_d.kshlib

#
# DESCRIPTION:
#	'zfs list -d <n>' should get expected output.
#
# STRATEGY:
#	1. 'zfs list -d <n>' to get the output.
#	2. 'zfs list -r|grep' to get the expected output.
#	3. Compare the two outputs, they should be same.
#

verify_runnable "both"

set -A fs_type "all" "filesystem" "snapshot"
if is_global_zone ; then
	set -A fs_type ${fs_type[*]} "volume"
fi

function cleanup
{
	log_must rm -f $DEPTH_OUTPUT $EXPECT_OUTPUT
}

log_onexit cleanup
log_assert "'zfs list -d <n>' should get expected output."

DEPTH_OUTPUT="$TEST_BASE_DIR/depth_output"
EXPECT_OUTPUT="$TEST_BASE_DIR/expect_output"
typeset -i old_val=0
typeset -i j=0
typeset -i fs=0
typeset eg_opt="$DEPTH_FS"$
for dp in ${depth_array[@]}; do
	(( j=old_val+1 ))
	while (( j<=dp && j<=MAX_DEPTH )); do
		eg_opt="$eg_opt""|depth""$j"$
		(( j+=1 ))
	done
	(( fs=0 ))
	while (( fs<${#fs_type[*]} )); do
		if [[ "$dp" == "0" ]] && \
		  [[ "${fs_type[$fs]}" == "volume" || "${fs_type[$fs]}" == "snapshot" ]]; then
			log_must eval "zfs list -H -d $dp -o name -t ${fs_type[$fs]} $DEPTH_FS > $DEPTH_OUTPUT"
			[[ -s "$DEPTH_OUTPUT" ]] && \
				log_fail "$DEPTH_OUTPUT should be null."
			log_mustnot zfs list -rH -o name -t ${fs_type[$fs]} $DEPTH_FS | grep -E "$eg_opt"
		else
			log_must eval "zfs list -H -d $dp -o name -t ${fs_type[$fs]} $DEPTH_FS > $DEPTH_OUTPUT"
			log_must eval "zfs list -rH -o name -t ${fs_type[$fs]} $DEPTH_FS | grep -E '$eg_opt' > $EXPECT_OUTPUT"
			log_must diff $DEPTH_OUTPUT $EXPECT_OUTPUT
		fi
		(( fs+=1 ))
	done
	(( old_val=dp ))
done

log_pass "'zfs list -d <n>' should get expected output."
