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
# Copyright (C) 2019 Aleksa Sarai <cyphar@cyphar.com>
# Copyright (C) 2019 SUSE LLC
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

function cleanup
{
	log_must rm -rf $TESTDIR/*
}

log_onexit cleanup

cd $TESTDIR
echo "foo" > foo
echo "bar" > bar

if is_freebsd; then
	#
	# ZFS on FreeBSD does not implement AT_RENAME_EXCHANGE, so the call
	# must fail with EOPNOTSUPP and leave both files as they were.  It
	# must not fall through to a plain rename, which would replace bar
	# with foo and destroy bar's data.  A kernel without
	# AT_RENAME_EXCHANGE (e.g. 15.1-RELEASE) rejects the flag itself
	# with EINVAL before it reaches ZFS, which is just as safe.
	#
	log_assert "ZFS on FreeBSD rejects AT_RENAME_EXCHANGE."

	typeset ino_foo=$(stat -f %i foo)
	typeset ino_bar=$(stat -f %i bar)

	typeset out
	out=$(renameat2 -x foo bar 2>&1)
	typeset -i rc=$?
	log_note "renameat2 -x foo bar: exit $rc: $out"
	(( rc == 1 )) || log_fail "exchange returned $rc, expected failure"
	[[ "$out" == *"Operation not supported"* ||
	    "$out" == *"Invalid argument"* ]] || \
	    log_fail "exchange failed with \"$out\", expected EOPNOTSUPP" \
	    "(or EINVAL from a kernel without AT_RENAME_EXCHANGE)"

	# Both names, contents and inodes are unchanged.
	log_must grep '^foo$' foo
	log_must grep '^bar$' bar
	log_must test "$(stat -f %i foo)" = "$ino_foo"
	log_must test "$(stat -f %i bar)" = "$ino_bar"

	# The directory is still usable: a plain rename works afterwards.
	log_must mv foo baz
	log_must mv baz foo
	log_must grep '^foo$' foo
	log_must grep '^bar$' bar

	# Exchange with a bad path still fails.
	log_mustnot renameat2 -x bar baz
	log_must grep '^bar$' bar

	log_pass "ZFS on FreeBSD rejects AT_RENAME_EXCHANGE as expected."
fi

log_assert "ZFS supports RENAME_EXCHANGE."

# Self-exchange is a no-op.
log_must renameat2 -x foo foo
log_must grep '^foo$' foo

# Basic exchange.
log_must renameat2 -x foo bar
log_must grep '^bar$' foo
log_must grep '^foo$' bar

# And exchange back.
log_must renameat2 -x foo bar
log_must grep '^foo$' foo
log_must grep '^bar$' bar

# Exchange with a bad path should fail.
log_mustnot renameat2 -x bar baz

log_pass "ZFS supports RENAME_EXCHANGE as expected."
