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

#
# DESCRIPTION:
# Setting the valid option and properties 'zfs get' return correct value.
# It should be successful.
#
# STRATEGY:
# 1. Create pool, filesystem, dataset, volume and snapshot.
# 2. Getting the options and properties random combination.
# 3. Using the combination as the parameters of 'zfs get' to check the
# command line return value.
#

verify_runnable "both"

typeset options=(" " p r H)

typeset zfs_props=("type" used available creation volsize referenced \
    compressratio mounted origin recordsize quota reservation mountpoint \
    sharenfs checksum compression atime devices exec readonly setuid \
    snapdir aclinherit canmount primarycache secondarycache version \
    usedbychildren usedbydataset usedbyrefreservation usedbysnapshots)
if is_freebsd; then
	typeset zfs_props_os=(jailed aclmode)
else
	typeset zfs_props_os=(zoned acltype)
fi
typeset userquota_props=(userquota@root groupquota@root userused@root \
    groupused@root)
typeset props=("${zfs_props[@]}" \
    "${zfs_props_os[@]}" \
    "${userquota_props[@]}")
typeset dataset=($TESTPOOL/$TESTCTR $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTFS@$TESTSNAP $TESTPOOL/$TESTVOL@$TESTSNAP)

log_assert "Setting the valid options and properties 'zfs get' return " \
    "correct value. It should be successful."
log_onexit cleanup

# Create volume and filesystem's snapshot
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP
create_snapshot $TESTPOOL/$TESTVOL $TESTSNAP

#
# Begin to test 'get [-prH] <property[,property]...>
#		<filesystem|dataset|volume|snapshot>'
#		'get [-prH] <-a|-d> <filesystem|dataset|volume|snapshot>"
#
typeset -i opt_numb=8
typeset -i prop_numb=20
for dst in ${dataset[@]}; do
	# option can be empty, so "" is necessary.
	for opt in "" $(gen_option_str "${options[*]}" "-" "" $opt_numb); do
		for prop in $(gen_option_str "${props[*]}" "" "," $prop_numb)
		do
			log_must eval "zfs get $opt $prop $dst > /dev/null 2>&1"
		done
	done
done

log_pass "Setting the valid options to dataset, 'zfs get' pass."
