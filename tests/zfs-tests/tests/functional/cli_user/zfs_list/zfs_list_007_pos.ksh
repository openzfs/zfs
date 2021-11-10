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
#	2. 'zfs list -r|egrep' to get the expected output.
#	3. Compare the two outputs, they should be same.
#

verify_runnable "both"

set -A fs_type "all" "filesystem" "snapshot"
if is_global_zone ; then
	set -A fs_type ${fs_type[*]} "volume"
fi

function cleanup
{
	log_must rm -f $DEPTH_OUTPUT
	log_must rm -f $EXPECT_OUTPUT
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
			log_mustnot zfs list -rH -o name -t ${fs_type[$fs]} $DEPTH_FS | egrep -e '$eg_opt'
		else
			log_must eval "zfs list -H -d $dp -o name -t ${fs_type[$fs]} $DEPTH_FS > $DEPTH_OUTPUT"
			log_must eval "zfs list -rH -o name -t ${fs_type[$fs]} $DEPTH_FS | egrep -e '$eg_opt' > $EXPECT_OUTPUT"
			log_must diff $DEPTH_OUTPUT $EXPECT_OUTPUT
		fi
		(( fs+=1 ))
	done
	(( old_val=dp ))
done

log_pass "'zfs list -d <n>' should get expected output."
