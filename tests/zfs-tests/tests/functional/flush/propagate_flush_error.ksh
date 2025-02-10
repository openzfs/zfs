#!/bin/ksh -p
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
# Copyright (c) 2025, Klara, Inc.
#

#
# This tests that flush errors are propagated to their corresponding writes,
# and thus are surfaced as write errors.
#
# The technique is simple: inject flush errors, then generate some writes.
# If the errors propagate, the pipeline will take action as though are
# were write errors. In the case of this test, that's simply to redirect
# the write to other vdevs.
#
# We don't try to match the number of flushes to writes, because there are
# other writes to the vdev (metadata & housekeeping) that will be handled
# differently. All we care is that we inject some flush errors, and we see
# some write errors.
#

. $STF_SUITE/include/libtest.shlib

TMPDIR=${TMPDIR:-$TEST_BASE_DIR}

VDEV_FILE_BASE="$TMPDIR/vdev_file_"

verify_runnable "global"

typeset devs=""

function cleanup
{
	zinject -c all
	zpool clear $TESTPOOL
	destroy_pool $TESTPOOL
	for dev in "$devs" ; do
		losetup -d $dev
	done
	rm -f $VDEV_FILE_BASE* || true
}

log_onexit cleanup

log_assert "verify that device flush errors are surfaced as write errors"

for n in $(seq 1 6) ; do
    typeset devfile=$VDEV_FILE_$n
    truncate -s 100M $devfile
    typeset dev=$(basename $(losetup -f))
    log_must losetup /dev/$dev $devfile
    devs="$devs $dev"
done

# create the pool. use a small recordsize so we can generate more writes, and
# turn off compression to make sure nothing gets clever with holes. disable
# sync to ensure the ZIL doesn't get in the way.
log_must zpool create -f \
    -O sync=disabled -O recordsize=16K -O compression=off \
    $TESTPOOL raidz1 $devs

# make flushes to one of the devices fail. the errors should be propagated
# to the writes and surfaced as write errors
log_must zinject -d $dev -e io -T flush $TESTPOOL

# create a "big" file, generating plenty of writes
log_must dd if=/dev/random of=/$TESTPOOL/data_file bs=8M count=1

# wait for them to write out
zpool sync

# check we got more than 0 write errors
typeset -i vdev_write_errors=$(zpool get -Hpo value write_errors $TESTPOOL $dev)
log_must test $vdev_write_errors -gt 0

# check we inject more than 0 flush errors
typeset -i inject_flush_errors=$(zinject | grep -m 1 -oE '[0-9]+$')
log_must test $inject_flush_errors -gt 0

log_pass "device flush errors are surfaced as write errors"
