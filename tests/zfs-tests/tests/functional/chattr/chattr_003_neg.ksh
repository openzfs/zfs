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
# Copyright (c) 2026, Kyle Evans <kevans@FreeBSD.org>
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	An append-only file cannot be removed, renamed, hard-linked or have
#	its mode changed, and entries of an append-only directory cannot be
#	removed or renamed.
#
# STRATEGY:
#	1. Create a file and a directory with a child, and make the file and
#	   the directory append-only
#	2. Verify that each operation fails with EPERM and changes nothing
#

verify_runnable "global"

typeset file=$TESTDIR/append_file
typeset dir=$TESTDIR/append_dir

function set_append
{
	if is_freebsd ; then
		chflags sappnd $1
	else
		chattr +a $1
	fi
}

function clear_append
{
	if is_freebsd ; then
		chflags nosappnd $1
	else
		chattr -a $1
	fi
}

function cleanup
{
	for f in $file $file.renamed $dir $TESTDIR/child; do
		[[ -e $f ]] && log_must clear_append $f
	done
	log_must rm -rf $file $file.renamed $file.link $dir $TESTDIR/child
}

log_onexit cleanup

log_assert "Append-only files and directories cannot be modified by name"

log_must eval "echo test > $file"
log_must chmod 0644 $file
log_must set_append $file

log_mustnot_expect "Operation not permitted" chmod 0600 $file
log_must [ "$(ls -l $file | cut -c1-10)" = "-rw-r--r--" ]
log_mustnot_expect "Operation not permitted" rm -f $file
log_must [ -e $file ]
log_mustnot_expect "Operation not permitted" mv $file $file.renamed
log_must [ -e $file ]
log_mustnot [ -e $file.renamed ]
log_mustnot_expect "Operation not permitted" ln $file $file.link
log_mustnot [ -e $file.link ]

log_must mkdir $dir
log_must eval "echo test > $dir/child"
log_must set_append $dir

log_mustnot_expect "Operation not permitted" rm -f $dir/child
log_must [ -e $dir/child ]
log_mustnot_expect "Operation not permitted" mv $dir/child $TESTDIR/child
log_must [ -e $dir/child ]
log_mustnot [ -e $TESTDIR/child ]

log_pass "Append-only files and directories cannot be modified by name"
