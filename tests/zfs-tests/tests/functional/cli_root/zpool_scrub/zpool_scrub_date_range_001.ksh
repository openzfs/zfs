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
#     1. Write a file.
#     2. Force a sync of everything via export/import.
#     3. Wait for one minute.
#     4. Repeat steps 1, 2, and 3 four two times.
#     5. Inject checksum errors into all 3 files.
#     6. Scrub the date range for the first file.
#     7. Verify that the first file is scrubbed.
#     8. Verify that newer files are not scrubbed.
#     9. Repeat steps 6–8 for each of the remaining 2 files.
#

verify_runnable "global"

function cleanup
{
	log_must zinject -c all
	rm -f $TESTDIR/*_file
	log_must restore_tunable SPA_NOTE_TXG_TIME
}

log_onexit cleanup

log_assert "Verifiy scrub, -E, and -S show expected status."

log_must save_tunable SPA_NOTE_TXG_TIME
log_must set_tunable64 SPA_NOTE_TXG_TIME 30

typeset -a date_list
for i in `seq 0 2`; do
	log_must sleep 60
	log_must zpool export $TESTPOOL
	log_must zpool import $TESTPOOL
	date_list+=("$(date '+%Y-%m-%d %H:%M')")

	log_must file_write -o create -f"$TESTDIR/${i}_file" \
	    -b 512 -c 2048 -dR

	log_must sleep 60
	log_must zpool export $TESTPOOL
	log_must zpool import $TESTPOOL
	date_list+=("$(date '+%Y-%m-%d %H:%M')")
done

for i in `seq 0 2`; do
	log_must zinject -t data -e checksum -f 100 $TESTDIR/${i}_file
done

for i in `seq 0 2`; do
	log_must zpool scrub -w -S "${date_list[$((i * 2))]}" -E "${date_list[$((i * 2 + 1))]}" $TESTPOOL
	log_must eval "zpool status -v $TESTPOOL | grep '${i}_file'"
	for j in `seq 0 2`; do
		if [ $i == $j ]; then
			continue
		fi
		log_mustnot eval "zpool status -v $TESTPOOL | grep '${j}_file'"
	done
done

log_pass "Verified scrub, -E, and -S show expected status."
