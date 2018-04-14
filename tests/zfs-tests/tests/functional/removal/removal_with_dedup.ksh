#! /bin/ksh -p
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2014, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

default_setup_noexit "$DISKS"
log_onexit default_cleanup_noexit

log_must zfs set dedup=on $TESTPOOL

FILE_CONTENTS="Leeloo Dallas mul-ti-pass."

echo $FILE_CONTENTS  >$TESTDIR/$TESTFILE0
echo $FILE_CONTENTS  >$TESTDIR/$TESTFILE1
echo $FILE_CONTENTS  >$TESTDIR/$TESTFILE2
log_must [ "x$(cat $TESTDIR/$TESTFILE0)" = "x$FILE_CONTENTS" ]
log_must [ "x$(cat $TESTDIR/$TESTFILE1)" = "x$FILE_CONTENTS" ]
log_must [ "x$(cat $TESTDIR/$TESTFILE2)" = "x$FILE_CONTENTS" ]

log_must zpool remove $TESTPOOL $REMOVEDISK
log_must wait_for_removal $TESTPOOL
log_mustnot vdevs_in_pool $TESTPOOL $REMOVEDISK

log_must dd if=/$TESTDIR/$TESTFILE0 of=/dev/null
log_must dd if=/$TESTDIR/$TESTFILE1 of=/dev/null
log_must dd if=/$TESTDIR/$TESTFILE2 of=/dev/null
log_must [ "x$(cat $TESTDIR/$TESTFILE0)" = "x$FILE_CONTENTS" ]
log_must [ "x$(cat $TESTDIR/$TESTFILE1)" = "x$FILE_CONTENTS" ]
log_must [ "x$(cat $TESTDIR/$TESTFILE2)" = "x$FILE_CONTENTS" ]

log_pass "Removed device not in use after removal."
