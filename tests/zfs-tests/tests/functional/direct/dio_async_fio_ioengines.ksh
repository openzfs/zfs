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
	check_dio_write_chksum_verify_failures $TESTPOOL "raidz" 0
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
		if [ -e /etc/os-release ] ; then
			source /etc/os-release
			if [ -n "$REDHAT_SUPPORT_PRODUCT_VERSION" ] &&
			    ((floor($REDHAT_SUPPORT_PRODUCT_VERSION) == 9)) ; then
				log_note "io_uring disabled on CentOS 9, fails " \
				"with 'Operation not permitted'"
			elif $(check_fio_ioengine -eq 0); then
				fio_async_ioengines+=" io_uring"
			else
				log_note "io_uring not supported by fio and " \
				    "will not be tested"
			fi
		else 
			if $(check_fio_ioengine); then
				fio_async_ioengines+=" io_uring"
	
			else
				log_note "io_uring not supported by fio and " \
				   "will not be tested"
			fi
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
