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
