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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2011, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# 'zfs set' should fail with invalid arguments
#
# STRATEGY:
# 1. Create an array of invalid arguments
# 1. Run zfs set with each invalid argument
# 2. Verify that zfs set returns error
#

verify_runnable "both"

log_assert "'zfs set' fails with invalid arguments"

set -A editable_props "quota" "reservation" "reserv" "volsize" "recordsize" "recsize" \
		"mountpoint" "checksum" "compression" "compress" "atime" \
		"devices" "exec" "setuid" "readonly" "snapdir" "aclmode" \
		"aclinherit" "canmount" "xattr" "copies" "version"
if is_freebsd; then
	editable_props+=("jailed")
else
	editable_props+=("zoned")
fi

for ds in $TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL \
	$TESTPOOL/$TESTFS@$TESTSNAP; do
	for badarg in "" "-" "-?"; do
		for prop in ${editable_props[@]}; do
			log_mustnot eval "zfs set $badarg $prop= $ds >/dev/null 2>&1"
		done
	done
done

log_pass "'zfs set' fails with invalid arguments as expected."
