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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/inuse/inuse.cfg

#
# DESCRIPTION:
# Newfs will interfere with devices and spare devices that are in use
# by exported pool.
#
# STRATEGY:
# 1. Create a regular|mirror|raidz|raidz2 pool with the given disk
# 2. Export the pool
# 3. Try to newfs against the disk, verify it succeeds as expect.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL1 || $ZPOOL import $TESTPOOL1 >/dev/null 2>&1

	destroy_pool -f $TESTPOOL1

	#
	# Tidy up the disks we used.
	#
	cleanup_devices $vdisks $sdisks

	if [[ -n "$LINUX" ]]; then
		for i in {0..2}; do
			dsk=$(eval echo \${kpartx_dsk$i})
			[[ -n "$dsk" ]] && $KPARTX -d $dsk
		done
	fi
}

function verify_assertion #slices
{
	typeset targets=$1

	for t in $targets; do
		$ECHO "y" | $NEWFS -v $t #> /dev/null 2>&1
		(( $? !=0 )) && \
			log_fail "newfs over exported pool " \
				"failes unexpected."
	done

	return 0
}

log_assert "Verify newfs over exported pool succeed."

log_onexit cleanup

set -A vdevs "" "mirror" "raidz" "raidz1" "raidz2"

typeset -i i=0

for num in 0 1 2 3 ; do
	eval typeset slice=\${FS_SIDE$num}
	disk=${slice%[sp][0-9]}
	slice=${slice##*[sp]}
	if [[ $WRAPPER == *"smi"* && \
		$disk == ${saved_disk} ]]; then
		cyl=$(get_endslice $disk ${saved_slice})
		log_must set_partition $slice "$cyl" $FS_SIZE $disk
	else
		log_must set_partition $slice "" $FS_SIZE $disk
	fi
	saved_disk=$disk
	saved_slice=$slice
done

while (( i < ${#vdevs[*]} )); do
	if [[ -n $SINGLE_DISK && -n ${vdevs[i]} ]]; then
		(( i = i + 1 ))
		continue
	fi

	if [[ -n "$LINUX" ]]; then
		# Startup loop devices for the three disks we're using
		vslices="" ; rawtargets="" # We're overwriting these below
		for num in {0..2}; do
			dsk=$(eval echo \${FS_DISK$num})
			eval typeset kpartx_dsk$num=$dsk
	
			if [[ -n "$dsk" ]]; then
				set -- $($KPARTX -asfv $dsk | head -n1)
				loop=${8##*/}
				[[ $num == 0 ]] && loop0=$loop
				[[ $num == 1 ]] && loop1=$loop

				# Override variable
				vslices="$vslices /dev/mapper/$loop"p1
				eval typeset disk$i="/dev/mapper/$loop"p1
				rawtargets=$(eval echo "$rawtargets \$disk$i")
			fi
		done

		# Override variable
		sslices="/dev/mapper/$loop0"p2
	fi

	create_pool $TESTPOOL1 ${vdevs[i]} $vslices spare $sslices
	log_must $ZPOOL export $TESTPOOL1
	verify_assertion "$rawtargets"
	cleanup_devices $vslices $sslices

	if [[ -n "$LINUX" ]]; then
		for num in {0..2}; do
			dsk=$(eval echo \${FS_DISK$num})
			[[ -n "$dsk" ]] && $KPARTX -d $dsk
		done
	fi

	(( i = i + 1 ))
done

log_pass "Newfs over exported pool succeed."
