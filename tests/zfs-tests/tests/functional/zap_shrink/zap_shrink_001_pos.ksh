#! /bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
