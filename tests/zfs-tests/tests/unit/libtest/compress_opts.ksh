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
# Copyright 2018, Richard Elling
#

#
# DESCRIPTION:
# Unit tests for the libtest.shlib shell functions
# Test for functions related to compression options
#

. $STF_SUITE/include/libtest.shlib

oneTimeSetUp() {
	MOCK_ZFS_OUTPUT=$(mktemp $TEST_BASE_DIR/zfs.XXXXXX)
}

zfs()
{
	cat $MOCK_ZFS_OUTPUT
}

oneTimeTearDown() {
	rm -f $MOCK_ZFS_OUTPUT
}

test_get_compress_opts()
{
	cat >$MOCK_ZFS_OUTPUT <<EOM
missing property argument
usage:
	get [-rHp] [-d max] [-o "all" | field[,...]]
	    [-t type[,...]] [-s source[,...]]
	    <"all" | property[,...]> [filesystem|volume|snapshot|bookmark] ...

The following properties are supported:

	PROPERTY       EDIT  INHERIT   VALUES

	available        NO       NO   <size>
	compression     YES      YES   on | off | lzjb | gzip | gzip-[1-9] | zle
EOM
	assertFalse "zfs_compress does not have lz4" \
	    "get_compress_opts zfs_compress | grep -w lz4 >/dev/null"
	assertFalse "zfs_compress does not have off" \
	    "get_compress_opts zfs_compress | grep -w off >/dev/null"
	assertFalse "zfs_set does not have lz4" \
	    "get_compress_opts zfs_set | grep -w lz4 >/dev/null"
	assertTrue "zfs_set has off" \
	    "get_compress_opts zfs_set | grep -w off >/dev/null"
}

test_get_compress_opts_lz4()
{
	cat >$MOCK_ZFS_OUTPUT <<EOM
missing property argument
usage:
	get [-rHp] [-d max] [-o "all" | field[,...]]
	    [-t type[,...]] [-s source[,...]]
	    <"all" | property[,...]> [filesystem|volume|snapshot|bookmark] ...

The following properties are supported:

	PROPERTY       EDIT  INHERIT   VALUES

	available        NO       NO   <size>
 compression     YES      YES   on | off | lzjb | gzip | gzip-[1-9] | zle | lz4
EOM
	assertTrue "zfs_compress has gzip" \
	    "get_compress_opts zfs_compress | grep -w gzip >/dev/null"
	assertTrue "zfs_compress has lz4" \
	    "get_compress_opts zfs_compress | grep -w lz4 >/dev/null"
	assertFalse "zfs_compress does not have off" \
	    "get_compress_opts zfs_compress | grep -w off >/dev/null"
	assertTrue "zfs_set has lz4" \
	    "get_compress_opts zfs_set | grep -w lz4 >/dev/null"
	assertTrue "zfs_set has off" \
	    "get_compress_opts zfs_set | grep -w off >/dev/null"
}

. $STF_SUITE/include/shunit2
