#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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

        for file in `ls $TESTDIR/file.*`; do
		log_must rm -f $file
        done
}


log_assert "Verify 'zpool clear' can clear errors of a storage pool."
log_onexit cleanup

#make raw files to create various configuration pools
typeset -i i=0
while (( i < 3 )); do
	log_must mkfile $FILESIZE $TESTDIR/file.$i

	(( i = i + 1 ))
done

fbase=$TESTDIR/file
set -A poolconf "mirror $fbase.0 $fbase.1 $fbase.2" \
                "raidz1 $fbase.0 $fbase.1 $fbase.2" \
                "raidz2 $fbase.0 $fbase.1 $fbase.2"

function check_err # <pool> [<vdev>]
{
	typeset pool=$1
	shift
	if (( $# > 0 )); then
		typeset	checkvdev=$1
	else
		typeset checkvdev=""
	fi
	typeset -i errnum=0
	typeset c_read=0
	typeset c_write=0
	typeset c_cksum=0
	typeset tmpfile=/var/tmp/file.$$
	typeset healthstr="pool '$pool' is healthy"
	typeset output="`zpool status -x $pool`"

	[[ "$output" ==  "$healthstr" ]] && return $errnum

	zpool status -x $pool | grep -v "^$" | grep -v "pool:" \
			| grep -v "state:" | grep -v "config:" \
			| grep -v "errors:" > $tmpfile
	typeset line
	typeset -i fetchbegin=1
	while read line; do
		if (( $fetchbegin != 0 )); then
                        echo $line | grep "NAME" >/dev/null 2>&1
                        (( $? == 0 )) && (( fetchbegin = 0 ))
                         continue
                fi

		if [[ -n $checkvdev ]]; then
			echo $line | grep $checkvdev >/dev/null 2>&1
			(( $? != 0 )) && continue
			c_read=`echo $line | awk '{print $3}'`
			c_write=`echo $line | awk '{print $4}'`
			c_cksum=`echo $line | awk '{print $5}'`
			if [ $c_read != 0 ] || [ $c_write != 0 ] || \
			    [ $c_cksum != 0 ]
			then
				(( errnum = errnum + 1 ))
			fi
			break
		fi

		c_read=`echo $line | awk '{print $3}'`
		c_write=`echo $line | awk '{print $4}'`
		c_cksum=`echo $line | awk '{print $5}'`
		if [ $c_read != 0 ] || [ $c_write != 0 ] || \
		    [ $c_cksum != 0 ]
		then
			(( errnum = errnum + 1 ))
		fi
	done <$tmpfile

	return $errnum
}

function do_testing #<clear type> <vdevs>
{
	typeset FS=$TESTPOOL1/fs
	typeset file=/$FS/f
	typeset type=$1
	shift
	typeset vdev="$@"

	log_must zpool create -f $TESTPOOL1 $vdev
	log_must zfs create $FS
	#
	# Fully fill up the zfs filesystem in order to make data block errors
	# zfs filesystem
	#
	typeset -i ret=0
	typeset -i i=0
	while true ; do
		file_write -o create -f $file.$i -b $BLOCKSZ -c $NUM_WRITES
		ret=$?
		(( $ret != 0 )) && break
		(( i = i + 1 ))
	done
	(( $ret != 28 )) && log_fail "file_write fails to fully fill up the $FS."

	#
	#Make errors to the testing pool by overwrite the vdev device with
	#/usr/bin/dd command. We donot want to have a full overwrite. That
	#may cause the system panic. So, we should skip the vdev label space.
	#
	(( i = $RANDOM % 3 ))
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
	dd if=/dev/zero of=$fbase.$i seek=512 bs=1024 count=$wcount conv=notrunc \
			> /dev/null 2>&1
	log_must sync
	log_must zpool scrub $TESTPOOL1
	# Wait for the completion of scrub operation
	while is_pool_scrubbing $TESTPOOL1; do
		sleep 1
	done

	check_err $TESTPOOL1 && \
		log_fail "No error generated."
	if [[ $type == "device" ]]; then
		log_must zpool clear $TESTPOOL1 $fbase.$i
		! check_err $TESTPOOL1 $fbase.$i && \
		    log_fail "'zpool clear' fails to clear error for $fbase.$i device."
	fi

	if [[ $type == "pool" ]]; then
		log_must zpool clear $TESTPOOL1
		! check_err $TESTPOOL1 && \
		    log_fail "'zpool clear' fails to clear error for pool $TESTPOOL1."
	fi

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
