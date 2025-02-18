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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Try each zfs(1) sub-command without parameters to make sure
# it returns an error.
#
# STRATEGY:
# 1. Create an array of parameters
# 2. For each parameter in the array, execute the sub-command
# 3. Verify an error is returned.
#

verify_runnable "both"


set -A args  "" "create" "create -s" "create -V" "create -s -V" \
    "destroy" "destroy -f" "destroy -r" "destroy -R" "destroy -rRf" \
    "snapshot" "snapshot -r" \
    "rollback" "rollback -r" "rollback -R" "rollback -f" "rollback -rRf" \
    "clone" "clone -p" "promote" "rename" "rename -p" "rename -r" "list blah" \
    "set" "get" "get -rHp" "get -o" "get -s" \
    "inherit" "inherit -r"  "quota=" \
    "set reservation=" "set atime=" "set checksum=" "set compression=" \
    "set type="  "set creation=" "set used=" "set available=" "set referenced=" \
    "set compressratio=" "set mounted=" "set origin=" "set quota=" \
    "set reservation=" "set volsize=" " set volblocksize=" "set recordsize=" \
    "set mountpoint=" "set devices=" "set exec=" "set setuid=" "set readonly=" \
    "set snapdir=" "set aclmode=" "set aclinherit=" \
    "set quota=blah" "set reservation=blah" "set atime=blah" "set checksum=blah" \
    "set compression=blah" \
    "upgrade blah" "mount blah" "mount -o" \
    "umount blah" "unmount" "unmount blah" "unmount -f" \
    "share" "unshare" "send" "send -i" "receive" "receive -d" "receive -vnF" \
    "recv" "recv -d" "recv -vnF" "allow" "unallow" \
    "blah blah" "-%" "--" "--?" "-*" "-="
if is_freebsd; then
	args+=("set jailed=")
else
	args+=("set zoned=")
fi

log_assert "Badly-formed zfs sub-command should return an error."

typeset -i i=0
while (( $i < ${#args[*]} )); do
	log_mustnot zfs ${args[i]}
	((i = i + 1))
done

log_pass "Badly formed zfs sub-commands fail as expected."
