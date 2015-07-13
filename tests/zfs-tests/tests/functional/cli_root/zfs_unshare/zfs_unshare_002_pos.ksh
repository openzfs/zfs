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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that 'zfs unshare [-a] <filesystem|mountpoint>' is aware of legacy share.
#
# STRATEGY:
# 1. Set 'zfs set sharenfs=off'
# 2. Use 'share' to share given filesystem
# 3. Verify that 'zfs unshare <filesystem|mountpoint>' is aware of legacy share
# 4. Verify that 'zfs unshare -a' is aware of legacy share.
#

verify_runnable "global"

function cleanup
{
	typeset -i i=0
	while (( i < ${#mntp_fs[*]} )); do
		if is_shared ${mntp_fs[i]}; then
			if [[ -n "$LINUX" ]]; then
				log_must $UNSHARE -u "*:${mntp_fs[i]}"
			else
				log_must $UNSHARE -F nfs ${mntp_fs[i]}
			fi
		fi

		((i = i + 2))
	done

	if mounted $TESTPOOL/$TESTCLONE; then
		log_must $ZFS unmount $TESTDIR2
	fi

	[[ -d $TESTDIR2 ]] && \
		log_must $RM -rf $TESTDIR2

	destroy_dataset -f $TESTPOOL/$TESTCLONE
	destroy_dataset -f $TESTPOOL/$TESTFS2@snapshot
	destroy_dataset -f $TESTPOOL/$TESTFS2
}

#
# Main test routine.
#
# Given a mountpoint and file system this routine will attempt
# to verify 'zfs unshare' is aware of legacy share.
#
function test_legacy_unshare # <mntp> <filesystem>
{
        typeset mntp=$1
        typeset filesystem=$2

	log_must $ZFS set sharenfs=off $filesystem
	not_shared $mntp || \
	    log_fail "'zfs set sharenfs=off' fails to make ZFS " \
	    "filesystem $filesystem unshared."

	if [[ -n "$LINUX" ]]; then
		log_must $SHARE -i "*:$mntp"
	else
		log_must $SHARE -F nfs $mntp
	fi
	is_shared $mntp || \
	    log_fail "'share' command fails to share ZFS file system."
	#
	# Verify 'zfs unshare <filesystem>' is aware of legacy share.
	#
	log_mustnot $ZFS unshare $filesystem
	is_shared $mntp || \
	    log_fail "'zfs unshare <filesystem>' fails to be aware" \
	    "of legacy share."

	#
	# Verify 'zfs unshare <filesystem>' is aware of legacy share.
	#
	log_mustnot $ZFS unshare $mntp
	is_shared $mntp || \
	    log_fail "'zfs unshare <mountpoint>' fails to be aware" \
	    "of legacy share."
}


set -A mntp_fs \
    "$TESTDIR" "$TESTPOOL/$TESTFS" \
    "$TESTDIR1" "$TESTPOOL/$TESTCTR/$TESTFS1" \
    "$TESTDIR2" "$TESTPOOL/$TESTCLONE"

log_assert "Verify that 'zfs unshare [-a]' is aware of legacy share."
log_onexit cleanup

log_must $ZFS create $TESTPOOL/$TESTFS2
log_must $ZFS snapshot $TESTPOOL/$TESTFS2@snapshot
log_must $ZFS clone $TESTPOOL/$TESTFS2@snapshot $TESTPOOL/$TESTCLONE
log_must $ZFS set mountpoint=$TESTDIR2 $TESTPOOL/$TESTCLONE

#
# Invoke 'test_legacy_unshare' routine to verify.
#
typeset -i i=0
while (( i < ${#mntp_fs[*]} )); do
	test_legacy_unshare ${mntp_fs[i]} ${mntp_fs[((i + 1 ))]}

	((i = i + 2))
done


log_note "Verify '$ZFS unshare -a' is aware of legacy share."

#
# set the 'sharenfs' property to 'off' for each filesystem
#
i=0
while (( i < ${#mntp_fs[*]} )); do
        log_must $ZFS set sharenfs=off ${mntp_fs[((i + 1))]}
	echo "DEBUG($i): not_shared ${mntp_fs[i]}"
        not_shared ${mntp_fs[i]} || \
                log_fail "'$ZFS set sharenfs=off' unshares file system failed."

        ((i = i + 2))
done

#
# Share each of the file systems via legacy share.
#
i=0
while (( i < ${#mntp_fs[*]} )); do
	if [[ -n "$LINUX" ]]; then
	        $SHARE -i "*:${mntp_fs[i]}"
	else
	        $SHARE -F nfs ${mntp_fs[i]}
	fi
        is_shared ${mntp_fs[i]} || \
                log_fail "'$SHARE' shares ZFS filesystem failed."

        ((i = i + 2))
done

#
# Verify that 'zfs unshare -a' is aware of legacy share
#
log_must $ZFS unshare -a

#
# verify ZFS filesystems are still shared
#
i=0
while (( i < ${#mntp_fs[*]} )); do
        is_shared ${mntp_fs[i]} || \
            log_fail "'$ZFS  unshare -a' fails to be aware of legacy share."

        ((i = i + 2))
done

log_pass "'$ZFS unshare [-a]' succeeds to be aware of legacy share."

