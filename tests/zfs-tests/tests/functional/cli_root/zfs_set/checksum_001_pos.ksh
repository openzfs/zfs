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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# Setting a valid checksum on a pool, file system, volume, it should be
# successful.
#
# STRATEGY:
# 1. Create pool, then create filesystem and volume within it.
# 2. Setting different valid checksum to each dataset.
# 3. Check the return value and make sure it is 0.
#

verify_runnable "both"

set -A dataset "$TESTPOOL" "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTVOL"
set -A values "on" "off" "fletcher2" "fletcher4" "sha256" "sha512" "skein" "edonr" "blake3" "noparity"

log_assert "Setting a valid checksum on a file system, volume," \
	"it should be successful."

typeset -i i=0
typeset -i j=0
while (( i < ${#dataset[@]} )); do
	j=0
	while (( j < ${#values[@]} )); do
		set_n_check_prop "${values[j]}" "checksum" "${dataset[i]}"
		(( j += 1 ))
	done
	(( i += 1 ))
done

log_pass "Setting a valid checksum on a file system, volume pass."
