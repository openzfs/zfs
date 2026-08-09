#! /bin/ksh -p
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
# Copyright 2024, Klara Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Create a large number of files in a directory. Then remove all files and
# check that the directory zap was shrunk. Use zdb to check that the zap object
# contains only one leaf block using zdb.
#

verify_runnable "global"

DIR=largedir

NR_FILES=100000
BATCH=1000
CWD=$PWD

log_assert "Create a large number of files ($NR_FILES) in a directory. " \
	"Make sure that the directory ZAP object was shrunk."

log_must mkdir $TESTDIR/$DIR

cd $TESTDIR/$DIR
# In order to prevent arguments overflowing, create NR_FILES in BATCH at once.
for i in $(seq $(($NR_FILES/$BATCH))); do
	touch $(seq $((($i-1)*$BATCH+1)) $(($i*$BATCH)));
done
cd $CWD

log_must test $NR_FILES -eq $(ls -U $TESTDIR/$DIR | wc -l)

# remove all files in $DIR directory
cd $TESTDIR/$DIR
for i in $(seq $(($NR_FILES/$BATCH))); do
	rm $(seq $((($i-1)*$BATCH+1)) $(($i*$BATCH)))
done
cd $CWD
sync_pool $TESTPOOL

log_must test 0 -eq $(ls -U $TESTDIR/$DIR | wc -l)

# check whether zap_shrink works
zapobj=$(zdb -v -O $TESTPOOL/$TESTFS $DIR)
nleafs=$(echo "$zapobj" | grep "Leaf blocks:" | awk -F\: '{print($2);}')
log_must test 1 -eq $nleafs

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

# check whether zap_shrink works
zapobj=$(zdb -v -O $TESTPOOL/$TESTFS $DIR)
nleafs=$(echo "$zapobj" | grep "Leaf blocks:" | awk -F\: '{print($2);}')
log_must test 1 -eq $nleafs

log_pass
