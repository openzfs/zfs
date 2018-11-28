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
# Copyright (c) 2014, 2017 by Delphix. All rights reserved.
# Copyright (c) 2018 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

TMPDIR=${TMPDIR:-$TEST_BASE_DIR}
DISK0=$TMPDIR/dsk0
DISK1=$TMPDIR/dsk1
DISK2=$TMPDIR/dsk2
DISK3=$TMPDIR/dsk3

log_must truncate -s $MINVDEVSIZE $DISK0 $DISK1
log_must truncate -s $((MINVDEVSIZE * 4)) $DISK2 $DISK3

function cleanup
{
	default_cleanup_noexit
	log_must rm -f $DISK0 $DISK1 $DISK2 $DISK3
}

default_setup_noexit "mirror $DISK0 $DISK1 mirror $DISK2 $DISK3"
log_onexit cleanup

#
# Fault the first side of mirror-0 and the second side of mirror-1.
# Verify that when the source and destination vdev mapping is setup
# only readable and writable children are selected.  Failure to do
# so would result in no valid copy of this data after removal.
#
# Default Mapping (BAD):
#   mirror-0                mirror-1
#     DISK0 (FAULTED) ------> DISK2
#     DISK1 ----------------> DISK3 (FAULTED)
#
# Required Mapping (GOOD):
#   mirror-0                mirror-1
#     DISK0 (FAULTED)   +---> DISK2
#     DISK1 ------------+     DISK3 (FAULTED)
#
log_must zpool offline -f $TESTPOOL $DISK0
log_must zpool offline -f $TESTPOOL $DISK3

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

log_must zpool remove $TESTPOOL mirror-0
log_must wait_for_removal $TESTPOOL
log_mustnot vdevs_in_pool $TESTPOOL mirror-0

verify_pool $TESTPOOL

if [[ -f $WORDS_FILE1 ]]; then
	log_must diff $WORDS_FILE1 $TESTDIR/words
elif [[ -f $WORDS_FILE2 ]]; then
	log_must diff $WORDS_FILE2 $TESTDIR/words
else
	log_must dd if=/$TESTDIR/$TESTFILE0 of=/dev/null
	log_must [ "x$(<$TESTDIR/$TESTFILE0)" = "x$FILE_CONTENTS" ]
fi

log_pass "Can remove with faulted vdevs"
