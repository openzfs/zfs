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
