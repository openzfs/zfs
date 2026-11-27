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
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	A resilver must copy a block that is cloned into a file the
#	resilver has already passed, after the clone's source file is
#	removed and the block has no other reference.  This holds when
#	the pool is exported before the resilver reads the block.
#
# STRATEGY:
#	1. Offline the second side of a mirror and create many empty
#	   files.  The one with the lowest object number is D and the
#	   one with the highest is S.  Write S.
#	2. Export and import to empty the ARC, delay reads of the
#	   first side and online the second.
#	3. behind: freeze the resilver once a pass starts past D,
#	   clone S onto D, remove S and let the resilver finish.  The
#	   first pass after the freeze must start between D and S, or
#	   the run missed the window and is repeated.
#	4. Detach the first side and verify D cold.  A failed read
#	   fails the test after the last arm.
#	5. legacy repeats behind on a legacy scan, which must not issue
#	   from the sorted queues while the other arms must.  gang
#	   repeats it with S written as a gang block, which zdb must
#	   show before the clone.
#	6. ahead freezes the resilver before it starts, so D is still
#	   ahead of it.  noclone freezes it between D and S and clones
#	   nothing, then verifies S.  Both must read back intact.
#	7. export repeats behind with a scan checkpoint every second.
#	   It freezes the resilver once a checkpoint follows a pass
#	   that started past D, and the bookmark saved on disk must
#	   lie between D and S.  After the clone and the removal, the
#	   saved bookmark must not have moved.  The pool is exported
#	   and imported before the resilver is released.
#	8. In behind and export the scan flags on disk must carry
#	   DSF_QUEUED_UNWALKED after the removal.  After the release,
#	   dbgmsg must show that behind cleared the flag once the
#	   hook's read was done, and that the import in export
#	   restarted the scan for it.  A failed check fails the test
#	   after the last arm.
#

verify_runnable "global"

log_assert "A resilver copies a block cloned behind it"
resilver_behind
log_pass "A resilver copied a block cloned behind it"
