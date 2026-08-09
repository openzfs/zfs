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

#
# Copyright (c) 2022 by Triad National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/properties.shlib
. $STF_SUITE/tests/functional/direct/dio.cfg
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
# 	Verify FIO async engines work using Direct I/O.
#
# STRATEGY:
#	1. Select a FIO async ioengine
#	2. Start sequntial Direct I/O and verify with buffered I/O
#	3. Start mixed Direct I/O and verify with buffered I/O
#

verify_runnable "global"

function cleanup
{
	log_must rm -f "$mntpnt/direct-*"
}

function check_fio_ioengine
{
	fio --ioengine=io_uring --parse-only > /dev/null 2>&1
	return $?
}

log_assert "Verify FIO async ioengines work using Direct I/O."

log_onexit cleanup

typeset -a async_ioengine_args=("--iodepth=4" "--iodepth=4 --thread")

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
fio_async_ioengines="posixaio"

if is_linux; then
	fio_async_ioengines+=" libaio"
	if $(grep -q "CONFIG_IO_URING=y" /boot/config-$(uname -r)); then
		if $(check_fio_ioengine); then
			fio_async_ioengines+=" io_uring"
		else
			log_note "io_uring not supported by fio and " \
			   "will not be tested"
		fi
	else
		log_note "io_uring not supported by kernel will not " \
		   "be tested"

	fi
fi

for ioengine in $fio_async_ioengines; do
	for ioengine_args in "${async_ioengine_args[@]}"; do
		for op in "rw" "randrw" "write"; do
			log_note "Checking Direct I/O with FIO async ioengine" \
			    " $ioengine with args $ioengine_args --rw=$op"
			dio_and_verify $op $DIO_FILESIZE $DIO_BS $mntpnt "$ioengine" \
			    "$ioengine_args"
		done
	done
done

log_pass "Verfied FIO async ioengines work using Direct I/O"
