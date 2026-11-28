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
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#

#
# DESCRIPTION:
#	Only a file's owner may change its extent size, even when others may
#	open it.
#
# STRATEGY:
#	1. As root, create a world-readable empty file and directory.
#	2. As another user, try to set and clear their extent sizes.
#	3. Check that nothing changed.
#

. $STF_SUITE/tests/functional/fileblocksize/fileblocksize.kshlib

verify_runnable "both"

function cleanup
{
	rm -rf $TESTDIR/owned $TESTDIR/owneddir $TESTDIR/pinned
	chmod $mode $TESTDIR
}

log_onexit cleanup
log_assert "Only the owner may change the extent size"

typeset mode=$(stat -c %a $TESTDIR 2>/dev/null || stat -f %Lp $TESTDIR)
log_must chmod 0777 $TESTDIR
log_must touch $TESTDIR/owned $TESTDIR/pinned
log_must mkdir $TESTDIR/owneddir
log_must extsize -s 4096 $TESTDIR/pinned
log_must chmod 0666 $TESTDIR/owned $TESTDIR/pinned
log_must chmod 0777 $TESTDIR/owneddir

for args in "-s 4096 $TESTDIR/owned" "-s 4096 $TESTDIR/owneddir" \
    "-c $TESTDIR/pinned"; do
	user_run $FBUSER extsize $args &&
	    log_fail "$FBUSER: extsize $args succeeded"
	out=$(<$TEST_BASE_DIR/out)
	[[ "$out" == "EPERM" || "$out" == "EACCES" ]] ||
	    log_fail "$FBUSER: extsize $args: $out"
done
verify_ext $TESTDIR/owned none 0
verify_ext $TESTDIR/owneddir none 0
verify_ext $TESTDIR/pinned extsize 4096

# the owner may
log_must chown $FBUSER $TESTDIR/owned
log_must user_run $FBUSER extsize -s 4096 $TESTDIR/owned
verify_ext $TESTDIR/owned extsize 4096

log_pass "Only the owner may change the extent size"
