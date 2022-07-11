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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_clear/zpool_clear.cfg

#
# DESCRIPTION:
# Verify 'zpool clear' can clear pool errors.
#
# STRATEGY:
# 1. Create various configuration pools
# 2. Make errors to pool
# 3. Use zpool clear to clear errors
# 4. Verify the errors has been cleared.
#

verify_runnable "global"

function cleanup
{
        poolexists $TESTPOOL1 && \
                log_must zpool destroy -f $TESTPOOL1

	log_must rm -f $fbase.{0..2}
}


log_assert "Verify 'zpool clear' can clear errors of a storage pool."
log_onexit cleanup

#make raw files to create various configuration pools
fbase=$TEST_BASE_DIR/file
log_must truncate -s $FILESIZE $fbase.{0..2}
set -A poolconf "mirror $fbase.0 $fbase.1 $fbase.2" \
                "raidz1 $fbase.0 $fbase.1 $fbase.2" \
                "raidz2 $fbase.0 $fbase.1 $fbase.2"

function check_err # <pool> [<vdev>]
{
	typeset pool=$1
	typeset	checkvdev=$2

	[ "$(zpool status -x $pool)" = "pool '$pool' is healthy" ] && return

	typeset -i skipstart=1
	typeset vdev _ c_read c_write c_cksum rest
	while read -r vdev _ c_read c_write c_cksum rest; do
		if [ $skipstart -ne 0 ]; then
			[ "$vdev" = "NAME" ] && skipstart=0
                        continue
                fi

		if [ -n "$checkvdev" ]; then
			[ "$vdev" = "$checkvdev" ] || continue
		fi

		[ $c_read$c_write$c_cksum = 000 ] || return
	done < <(zpool status -x $pool | grep -ve "^$" -e "pool:" -e "state:" -e "config:" -e "errors:")
}

function do_testing #<clear type> <vdevs>
{
	typeset FS=$TESTPOOL1/fs
	typeset file=/$FS/f
	typeset type=$1
	shift
	typeset vdev="$@"
	(( i = $RANDOM % 3 ))

	log_must zpool create -f $TESTPOOL1 $vdev
	log_must zfs create $FS
	#
	# Partially fill up the zfs filesystem in order to make data block
	# errors.  It's not necessary to fill the entire filesystem.
	#
	avail=$(get_prop available $FS)
	fill_mb=$(((avail / 1024 / 1024) * 25 / 100))
	log_must dd if=/dev/urandom of=$file bs=$BLOCKSZ count=$fill_mb

	#
	# Make errors to the testing pool by overwrite the vdev device with
	# dd command. We do not want to have a full overwrite. That
	# may cause the system panic. So, we should skip the vdev label space.
	#
	typeset -i wcount=0
	typeset -i size
	case $FILESIZE in
		*g|*G)
			(( size = ${FILESIZE%%[g|G]} ))
			(( wcount = size*1024*1024 - 512 ))
			;;
		*m|*M)
			(( size = ${FILESIZE%%[m|M]} ))
			(( wcount = size*1024 - 512 ))
			;;
		*k|*K)
			(( size = ${FILESIZE%%[k|K]} ))
			(( wcount = size - 512 ))
			;;
		*)
			(( wcount = FILESIZE/1024 - 512 ))
			;;
	esac
	dd if=/dev/zero of=$fbase.$i seek=512 bs=1024 count=$wcount conv=notrunc 2>/dev/null
	sync_all_pools
	log_must sync #ensure the vdev files are written out
	log_must zpool scrub -w $TESTPOOL1

	log_mustnot check_err $TESTPOOL1
	typeset dev=
	if [ "$type" = "device" ]; then
		dev=$fbase.$i
	fi

	log_must zpool clear $TESTPOOL1 $dev
	log_must check_err $TESTPOOL1 $dev

	log_must zpool destroy $TESTPOOL1
}

log_note "'zpool clear' clears leaf-device error."
for devconf in "${poolconf[@]}"; do
	do_testing "device" $devconf
done
log_note "'zpool clear' clears top-level pool error."
for devconf in "${poolconf[@]}"; do
	do_testing "pool" $devconf
done

log_pass "'zpool clear' clears pool errors as expected."
