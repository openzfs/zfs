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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A pool property whose value is not of the type of the property, or
#	is a string too long to be stored, is refused with an error and
#	leaves the pool as it was.  "zpool set" never sends one.
#
# STRATEGY:
#	1. Create a pool on a file vdev.
#	2. With pool_set_props, which passes a property list to the kernel
#	   as it is, send values of the wrong type, then strings and a name
#	   that are too long: each is refused with the expected error.
#	3. Check that none of them was set, that a property sent along
#	   with a refused one was not set either, and that the pool is
#	   healthy and can be written to.
#	4. Send well-formed values the same way: they are set.
#

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1
	rm -f $FILEDEV
}

# Send the properties in one call and check which error it returns.
function refused # error property...
{
	typeset expected=$1
	typeset err
	shift

	err=$(pool_set_props $TESTPOOL1 "$@")
	[[ "$err" == "$expected" ]] ||
	    log_fail "${1%%=*}: expected $expected, got '${err:-no error}'"
	log_note "${1%%=*} refused with $expected"
}

function check_prop # property expected
{
	typeset val=$(get_pool_prop $1 $TESTPOOL1)

	[[ "$val" == "$2" ]] || log_fail "$1 is '$val', expected '$2'"
}

log_assert "A pool property of the wrong type or too long is refused"
log_onexit cleanup

FILEDEV="$TEST_BASE_DIR/filedev.$$"
log_must truncate -s $MINVDEVSIZE $FILEDEV
log_must zpool create -f $TESTPOOL1 $FILEDEV

# A string value stops below 8192 bytes and a name below 256.
long=$(printf '%8192s' | tr ' ' v)
longname="$(printf '%254s' | tr ' ' n):p"

# The wrong type.  altroot comes first: a kernel that does not check it
# ignores it, while it stops in a VERIFY on some of those that follow.
refused EINVAL altroot=uint64:1
refused EINVAL readonly=string:on
refused EINVAL org.zfstest:prop=uint64:5
refused EINVAL org.zfstest:prop=boolean
refused EINVAL comment=uint64:5
refused EINVAL compatibility=uint64:5
refused EINVAL compatibility=boolean
refused EINVAL ashift=string:twelve
refused EINVAL ashift=boolean
refused EINVAL tname=uint64:5
refused EINVAL autotrim=string:on
refused EINVAL failmode=uint64:3

# Too long.
refused E2BIG org.zfstest:prop=string:$long
refused E2BIG compatibility=string:$long
refused E2BIG tname=string:$long
refused E2BIG name=string:$long
refused E2BIG "comment=string:$(printf '%33s' | tr ' ' c)"
refused ENAMETOOLONG $longname=string:value

# A well-formed property sent with a refused one is not set either.
refused EINVAL org.zfstest:other=string:value ashift=string:twelve

check_prop org.zfstest:prop -
check_prop org.zfstest:other -
check_prop comment -
check_prop compatibility off
check_prop ashift 0
check_prop autotrim off
check_prop failmode wait
log_must zpool get all $TESTPOOL1
check_prop health ONLINE
log_must dd if=/dev/urandom of=/$TESTPOOL1/file bs=128k count=8
log_must zpool sync $TESTPOOL1
check_prop health ONLINE

# Well-formed values are set, up to the longest string.
log_must pool_set_props $TESTPOOL1 org.zfstest:prop=string:value \
    comment=string:hello compatibility=string:legacy ashift=uint64:12
check_prop org.zfstest:prop value
check_prop comment hello
check_prop compatibility legacy
check_prop ashift 12
log_must pool_set_props $TESTPOOL1 org.zfstest:prop=string:${long%v}
[[ "$(get_pool_prop org.zfstest:prop $TESTPOOL1)" == "${long%v}" ]] ||
    log_fail "org.zfstest:prop does not have its 8191 bytes"
log_must pool_set_props $TESTPOOL1 org.zfstest:prop=string:
check_prop org.zfstest:prop -

log_pass "A pool property of the wrong type or too long is refused"
