#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2021 Matt Fiddaman
#

. $STF_SUITE/tests/functional/cli_root/zfs_get/zfs_get_common.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_get/zfs_get_list_d.kshlib

#
# DESCRIPTION:
# Setting the valid option and properties, 'zfs get' should return the
# correct property value.
#
# STRATEGY:
# 1. Create pool, filesystem, volume, snapshot, and bookmark.
# 2. Setting valid parameter, 'zfs get' should succeed.
# 3. Compare the output property name with the original input property.
#

verify_runnable "both"

typeset options=("" "-p" "-r" "-H")

typeset -i i=${#options[*]}
typeset -i j=0
while ((j<${#depth_options[*]}));
do
	options[$i]=-"${depth_options[$j]}"
	((j+=1))
	((i+=1))
done

typeset -r uint64_max="18446744073709551615"

typeset zfs_props=("type" used available creation volsize referenced \
    compressratio mounted origin recordsize quota reservation mountpoint \
    sharenfs checksum compression atime devices exec readonly setuid \
    snapdir aclinherit canmount primarycache secondarycache version \
    usedbychildren usedbydataset usedbyrefreservation usedbysnapshots \
    filesystem_limit snapshot_limit filesystem_count snapshot_count)
if is_freebsd; then
	typeset zfs_props_os=(jailed aclmode)
else
	typeset zfs_props_os=(zoned acltype)
fi
typeset userquota_props=(userquota@root groupquota@root userused@root \
    groupused@root)
typeset all_props=("${zfs_props[@]}" \
    "${zfs_props_os[@]}" \
    "${userquota_props[@]}")
typeset dataset=($TESTPOOL/$TESTCTR $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTFS@$TESTSNAP $TESTPOOL/$TESTVOL@$TESTSNAP
	$TESTPOOL/$TESTFS@$TESTSNAP1 $TESTPOOL/$TESTCLONE)

typeset bookmark_props=(creation)
typeset bookmark=($TESTPOOL/$TESTFS#$TESTBKMARK $TESTPOOL/$TESTVOL#$TESTBKMARK)

#
# According to dataset and option, checking if 'zfs get' return correct
# property information.
#
# $1 dataset
# $2 properties which are expected to output into $TESTDIR/$TESTFILE0
# $3 option
#
function check_return_value
{
	typeset dst=$1
	typeset props=$2
	typeset opt=$3
	typeset -i found=0
	typeset p

	for p in $props; do
		found=0

		while read line; do
			typeset item value _

			read -r _ item _ <<<"$line"
			if [[ $item == $p ]]; then
				((found += 1))
				cols=$(echo $line | awk '{print NF}')
			fi

			read -r _ _ value _ <<<"$line"
			if [[ $value == $uint64_max ]]; then
				log_fail "'zfs get $opt $props $dst' return " \
				    "UINT64_MAX constant."
			fi

			if ((found > 0)); then
				break
			fi
		done < $TESTDIR/$TESTFILE0

		if ((found == 0)); then
			log_fail "'zfs get $opt $props $dst' return " \
			    "error message.'$p' haven't been found."
		elif [[ "$opt" == "-p" ]] && ((cols != 4)); then
			log_fail "'zfs get $opt $props $dst' returned " \
			    "$cols columns instead of 4."
		fi
	done

	log_note "SUCCESS: 'zfs get $opt $prop $dst'."
}

log_assert "Setting the valid options and properties 'zfs get' should return " \
    "the correct property value."
log_onexit cleanup

# Create filesystem and volume's snapshot
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP
create_snapshot $TESTPOOL/$TESTVOL $TESTSNAP

# Create second snapshot and clone it
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP1
create_clone $TESTPOOL/$TESTFS@$TESTSNAP1 $TESTPOOL/$TESTCLONE

# Create filesystem and volume's bookmark
create_bookmark $TESTPOOL/$TESTFS $TESTSNAP $TESTBKMARK
create_bookmark $TESTPOOL/$TESTVOL $TESTSNAP $TESTBKMARK

typeset -i i=0
while ((i < ${#dataset[@]})); do
	for opt in "${options[@]}"; do
		for prop in ${all_props[@]}; do
			log_must eval "zfs get $opt $prop ${dataset[i]} > $TESTDIR/$TESTFILE0"
			check_return_value ${dataset[i]} "$prop" "$opt"
		done
	done
	((i += 1))
done

i=0
while ((i < ${#bookmark[@]})); do
	for opt in "${options[@]}"; do
		for prop in ${bookmark_props[@]}; do
			log_must eval "zfs get $opt $prop ${bookmark[i]} > $TESTDIR/$TESTFILE0"
			check_return_value ${bookmark[i]} "$prop" "$opt"
		done
	done
	((i += 1))
done

log_pass "Setting the valid options to dataset, it should succeed and return " \
    "valid value. 'zfs get' pass."
