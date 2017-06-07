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

. $STF_SUITE/tests/functional/cli_root/zfs_get/zfs_get_common.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_get/zfs_get_list_d.kshlib

#
# DESCRIPTION:
# Verify "-d <n>" can work with other options
#
# STRATEGY:
# 1. Create pool, filesystem, dataset, volume and snapshot.
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
	checksum compression atime devices exec readonly setuid zoned snapdir \
	aclmode aclinherit canmount primarycache secondarycache \
	usedbychildren usedbydataset usedbyrefreservation usedbysnapshots \
	userquota@root groupquota@root userused@root groupused@root

$ZFS upgrade -v > /dev/null 2>&1
if [[ $? -eq 0 ]]; then
	set -A all_props ${all_props[*]} version
fi

set -A dataset $TESTPOOL/$TESTCTR $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTFS@$TESTSNAP $TESTPOOL/$TESTVOL@$TESTSNAP

log_assert "Verify '-d <n>' can work with other options"
log_onexit cleanup

# Create volume and filesystem's snapshot
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP
create_snapshot $TESTPOOL/$TESTVOL $TESTSNAP

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
			log_must eval "$ZFS get -${depth_options[depth_item]} ${options[item]} $prop $dst > /dev/null 2>&1"
		done
		(( i += 1 ))
	done
done

log_pass "Verify '-d <n>' can work with other options"

