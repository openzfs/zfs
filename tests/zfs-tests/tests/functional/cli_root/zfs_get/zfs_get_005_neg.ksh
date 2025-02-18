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
#

. $STF_SUITE/tests/functional/cli_root/zfs_get/zfs_get_common.kshlib

#
# DESCRIPTION:
# Setting the invalid option and properties, 'zfs get' should failed.
#
# STRATEGY:
# 1. Create pool, filesystem, volume and snapshot.
# 2. Getting incorrect combination by invalid parameters
# 3. Using the combination as the parameters of 'zfs get' to check the
# command line return value.
#

verify_runnable "both"

typeset val_opts=(p r H)
typeset v_props=(type used available creation volsize referenced compressratio \
    mounted origin recordsize quota reservation mountpoint sharenfs checksum \
    compression atime devices exec readonly setuid snapdir version \
    aclinherit canmount primarycache secondarycache \
    usedbychildren usedbydataset usedbyrefreservation usedbysnapshots)
if is_freebsd; then
	typeset v_props_os=(jailed aclmode)
else
	typeset v_props_os=(zoned acltype)
fi
typeset  userquota_props=(userquota@root groupquota@root userused@root \
    groupused@root)
typeset val_props=("${v_props[@]}" \
    "${v_props_os[@]}" \
    "${userquota_props[@]}")
set -f	# Force shell does not parse '?' and '*' as the wildcard
typeset inval_opts=(P R h ? *)
typeset inval_props=(Type 0 ? * -on --on readonl time USED RATIO MOUNTED)

typeset dataset=($TESTPOOL/$TESTFS $TESTPOOL/$TESTCTR $TESTPOOL/$TESTVOL \
    $TESTPOOL/$TESTFS@$TESTSNAP $TESTPOOL/$TESTVOL@$TESTSNAP)

typeset -i opt_numb=6
typeset -i prop_numb=12

val_opts_str=$(gen_option_str "${val_opts[*]}" "-" "" $opt_numb)
val_props_str=$(gen_option_str "${val_props[*]}" "" "," $prop_numb)

inval_opts_str=$(gen_option_str "${inval_opts[*]}" "-" "" $opt_numb)
inval_props_str=$(gen_option_str "${inval_props[*]}" "" "," $prop_numb)

typeset val_bookmark_props=(creation)
typeset bookmark=($TESTPOOL/$TESTFS#$TESTBKMARK $TESTPOOL/$TESTVOL#$TESTBKMARK)

#
# Test different options and properties combination.
#
# $1 options
# $2 properties
#
function test_options
{
	typeset opts=$1
	typeset props=$2

	for dst in ${dataset[@]}; do
		for opt in $opts; do
			for prop in $props; do
				log_mustnot eval "zfs get $opt -- $prop $dst > /dev/null 2>&1"
			done
		done
	done
}

#
# Test different options and properties combination for bookmarks.
#
# $1 options
# $2 properties
#
function test_options_bookmarks
{
	typeset opts=$1
	typeset props=$2

	for dst in ${bookmark[@]}; do
		for opt in $opts; do
			for prop in $props; do
				log_mustnot eval "zfs get $opt -- $prop $dst > /dev/null 2>&1"
			done
		done
	done
}

log_assert "Setting the invalid option and properties, 'zfs get' should be \
    failed."
log_onexit cleanup

# Create filesystem and volume's snapshot
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP
create_snapshot $TESTPOOL/$TESTVOL $TESTSNAP

# Create filesystem and volume's bookmark
create_bookmark $TESTPOOL/$TESTFS $TESTSNAP $TESTBKMARK
create_bookmark $TESTPOOL/$TESTVOL $TESTSNAP $TESTBKMARK

log_note "Valid options + invalid properties, 'zfs get' should fail."
test_options "$val_opts_str" "$inval_props_str"
test_options_bookmarks "$val_opts_str" "$inval_props_str"

log_note "Invalid options + valid properties, 'zfs get' should fail."
test_options "$inval_opts_str" "$val_props_str"
test_options_bookmarks "$inval_opts_str" "$val_bookmark_props"

log_note "Invalid options + invalid properties, 'zfs get' should fail."
test_options "$inval_opts_str" "$inval_props_str"
test_options_bookmarks "$inval_opts_str" "$inval_props_str"

log_pass "Setting the invalid options to dataset, 'zfs get' pass."
