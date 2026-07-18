#!/bin/ksh -p
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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# "zpool initialize -z" writes zeroes instead of the default pattern, and the
# chosen value is persisted per device.
#
# STRATEGY:
# 1. Pre-fill the backing file with a non-zero byte pattern, so that finding
#    zeroes afterwards actually proves -z did the work (rather than the check
#    passing on space that happened to already be zero).
# 2. Create a one-disk pool and run "zpool initialize -w -z".
# 3. Verify the zero fill value is persisted in the leaf ZAP.
# 4. Verify the pre-existing non-zero pattern is gone from every metaslab.
#

function cleanup
{
	if datasetexists $TESTPOOL; then
		zpool destroy -f $TESTPOOL
	fi
	if [[ -d "$TESTDIR" ]]; then
		rm -rf "$TESTDIR"
	fi
}
log_onexit cleanup

log_assert "'zpool initialize -z' writes zeroes and persists the choice"

SMALLFILE="$TESTDIR/smallfile"

log_must mkdir "$TESTDIR"
# Fill the whole device with 0xff so free space starts out non-zero.
log_must eval "tr '\0' '\377' < /dev/zero | head -c $MINVDEVSIZE > $SMALLFILE"

log_must zpool create $TESTPOOL "$SMALLFILE"
log_must zpool initialize -w -z $TESTPOOL

# The zero fill value must be recorded in the leaf ZAP.
log_must eval "zdb -dddd $TESTPOOL | \
    grep -qE 'org.openzfs:vdev_initialize_value = 0( |\$)'"

log_must zpool export $TESTPOOL

# The device started out entirely 0xff.  After zeroing, its free space (the
# large majority of a freshly created pool) must actually contain zeroes: the
# 0xff pre-fill must be gone AND zero words must dominate.  Requiring zeroes to
# dominate (rather than only "0xff is gone") also catches a regression that
# wrote some other non-zero value.  Use "-t x4" (not x8, unsupported on
# FreeBSD) and count both patterns in a single pass.
set -A counts $(od -An -tx4 -v "$SMALLFILE" | awk '
    { for (i = 1; i <= NF; i++) {
        n++
        if ($i == "ffffffff") f++
        else if ($i == "00000000") z++
    } }
    END { print f + 0, z + 0, n + 0 }')
ffwords=${counts[0]}
zwords=${counts[1]}
nwords=${counts[2]}
log_note "after -z: 0xff words=$ffwords zero words=$zwords of $nwords"

if [[ $nwords -eq 0 ]]; then
	log_fail "Could not read back the device"
elif [[ $ffwords -ge $((nwords / 2)) ]]; then
	log_fail "0xff pre-fill still present after 'initialize -z'"
elif [[ $zwords -lt $((nwords / 2)) ]]; then
	log_fail "free space is not predominantly zero after 'initialize -z'"
else
	log_pass "'zpool initialize -z' zeroed the pool's free space"
fi
