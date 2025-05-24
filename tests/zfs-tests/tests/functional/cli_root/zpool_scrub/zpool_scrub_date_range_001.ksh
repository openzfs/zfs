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
# Copyright 2025 Klara, Inc.
# Copyright 2025 Mariusz Zaborski <oshogbo@FreeBSD.org>
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#       Verify that the date range scrub only scrubs the files that were
#       created/modified within a given time slot.
#
# STRATEGY:
#     1. Move current date to 10 days ago.
#     2. Create a new pool so that the txg database has its first entry from this date.
#     3. Write a file.
#     4. Force a sync of everything via export/import.
#     5. Move the current date forward by one day.
#     6. Repeat steps 3, 4, and 5 nine more times.
#     7. Inject checksum errors into all 10 files.
#     8. Scrub the date range for the first file.
#     9. Verify that the first file is scrubbed.
#    10. Verify that newer files are not scrubbed.
#    11. Repeat steps 8–10 for each of the remaining 9 files.
#

verify_runnable "global"

curtime_diff=0

function cleanup
{
	log_must zinject -c all
	log_must date --set="+${curtime_diff} days"
	rm -f /$TESTPOOL2/*_file
	log_must zpool destroy -f $TESTPOOL2
	rm -f $TESTDIR/vdev_a
}

function move_time_and_write
{
	fname="${1}"
	tdiff="${2}"

	log_must dd if=/dev/random of=/$TESTPOOL2/${fname} bs=1M count=1
	log_must zpool export $TESTPOOL2
	log_must zpool import -d $TESTDIR $TESTPOOL2

	log_must date --set="+1 days"
	curtime_diff="${tdiff}"
}

log_onexit cleanup

log_assert "Verifiy scrub, -E, and -S show expected status."

log_must truncate -s $MINVDEVSIZE $TESTDIR/vdev_a
log_must date --set="-10 days"
log_must zpool create -o failmode=continue $TESTPOOL2 $TESTDIR/vdev_a

curtime_diff=10

typeset -a data_list
for i in `seq 10`; do
	date_list+=($(date "+%Y-%m-%d"))
	move_time_and_write "$((i - 1))_file" "$((10 - i))"
done
date_list+=($(date "+%Y-%m-%d"))

for i in `seq 0 9`; do
	log_must zinject -t data -e checksum -f 100 /$TESTPOOL2/${i}_file
done

for i in `seq 0 9`; do
	log_must zpool scrub -w -S ${date_list[$i]} -E ${date_list[$((i + 1))]} $TESTPOOL2
	log_must eval "zpool status -v $TESTPOOL2 | grep '${i}_file'"
	for j in `seq 0 9`; do
		if [ $i == $j ]; then
			continue
		fi
		log_mustnot eval "zpool status -v $TESTPOOL2 | grep '${j}_file'"
	done
done

log_pass "Verified scrub, -E, and -S show expected status."
