#!/bin/ksh -p
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
