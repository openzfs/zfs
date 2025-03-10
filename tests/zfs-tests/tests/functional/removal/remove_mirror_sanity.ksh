#! /bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

read -r DISK1 DISK2 DISK3 _ <<<"$DISKS"
DISKS="$DISK1 $DISK2 $DISK3"

log_must default_setup_noexit "$DISK1 mirror $DISK2 $DISK3"
log_onexit default_cleanup_noexit

WORDS_FILE1="/usr/dict/words"
WORDS_FILE2="/usr/share/dict/words"
FILE_CONTENTS="Leeloo Dallas mul-ti-pass."

if [[ -f $WORDS_FILE1 ]]; then
    log_must cp $WORDS_FILE1 $TESTDIR
elif [[ -f $WORDS_FILE2 ]]; then
    log_must cp $WORDS_FILE2 $TESTDIR
else
    echo $FILE_CONTENTS  >$TESTDIR/$TESTFILE0
    log_must [ "x$(<$TESTDIR/$TESTFILE0)" = "x$FILE_CONTENTS" ]
fi

log_must zpool remove $TESTPOOL mirror-1
log_must wait_for_removal $TESTPOOL
log_mustnot vdevs_in_pool $TESTPOOL mirror-1

if [[ -f $WORDS_FILE1 ]]; then
    log_must diff $WORDS_FILE1 $TESTDIR/words
elif [[ -f $WORDS_FILE2 ]]; then
    log_must diff $WORDS_FILE2 $TESTDIR/words
else
    log_must dd if=/$TESTDIR/$TESTFILE0 of=/dev/null
    log_must [ "x$(<$TESTDIR/$TESTFILE0)" = "x$FILE_CONTENTS" ]
fi

log_pass "Removed top-level mirror device not in use after removal."
