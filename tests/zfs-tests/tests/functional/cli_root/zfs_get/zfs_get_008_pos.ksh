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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zfs_get/zfs_get_common.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_get/zfs_get_list_d.kshlib

#
# DESCRIPTION:
# Verify "-d <n>" can work with other options
#
# STRATEGY:
# 1. Create pool, filesystem, dataset, volume, snapshot, and bookmark.
# 2. Getting an -d option, other options and properties random combination.
# 3. Using the combination as the parameters of 'zfs get' to check the
# command line return value.
#

verify_runnable "both"

set -A options " " "-r" "-H" "-p" "-rHp" "-o name" \
	"-s local,default,temporary,inherited,none" \
	"-o name -s local,default,temporary,inherited,none" \
	"-rHp -o name -s local,default,temporary,inherited,none"

set -A props type used available creation volsize referenced compressratio \
	mounted origin recordsize quota reservation mountpoint sharenfs \
	checksum compression atime devices exec readonly setuid snapdir \
	aclinherit canmount primarycache secondarycache \
	usedbychildren usedbydataset usedbyrefreservation usedbysnapshots \
	userquota@root groupquota@root userused@root groupused@root
if is_freebsd; then
	set -A props ${props[*]} jailed aclmode
else
	set -A props ${props[*]} zoned acltype
fi

zfs upgrade -v > /dev/null 2>&1
if [[ $? -eq 0 ]]; then
	set -A props ${props[*]} version
fi

set -A dataset $TESTPOOL/$TESTCTR $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTFS@$TESTSNAP $TESTPOOL/$TESTVOL@$TESTSNAP

set -A bookmark_props creation
set -A bookmark $TESTPOOL/$TESTFS#$TESTBKMARK $TESTPOOL/$TESTVOL#$TESTBKMARK

log_assert "Verify '-d <n>' can work with other options"
log_onexit cleanup

# Create volume and filesystem's snapshot
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP
create_snapshot $TESTPOOL/$TESTVOL $TESTSNAP

# Create filesystem and volume's bookmark
create_bookmark $TESTPOOL/$TESTFS $TESTSNAP $TESTBKMARK
create_bookmark $TESTPOOL/$TESTVOL $TESTSNAP $TESTBKMARK

typeset -i opt_numb=16
typeset -i prop_numb=16
typeset -i i=0
typeset -i item=0
typeset -i depth_item=0

for dst in ${dataset[@]}; do
	(( i=0 ))
	while (( i < opt_numb )); do
		(( item = $RANDOM % ${#options[@]} ))
		(( depth_item = $RANDOM % ${#depth_options[@]} ))
		for prop in $(gen_option_str "${props[*]}" "" "," $prop_numb)
		do
			log_must eval "zfs get -${depth_options[depth_item]} ${options[item]} $prop $dst > /dev/null 2>&1"
		done
		(( i += 1 ))
	done
done

for dst in ${bookmark[@]}; do
	(( i=0 ))
	while (( i < opt_numb )); do
		(( item = $RANDOM % ${#options[@]} ))
		(( depth_item = $RANDOM % ${#depth_options[@]} ))
		log_must eval "zfs get -${depth_options[depth_item]} ${options[item]} $bookmark_props $dst > /dev/null 2>&1"
		(( i += 1 ))
	done
done

log_pass "Verify '-d <n>' can work with other options"

