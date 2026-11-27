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
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#

#
# DESCRIPTION:
#	Directory inheritance works with ACLs large enough to push the new
#	object's system attributes from the bonus buffer into a spill block.
#
# STRATEGY:
#	1. For dnodesize=legacy and auto, grow an inheritable ACL on a
#	   directory one entry at a time (default POSIX ACL entries on Linux,
#	   NFSv4 entries with file and directory inheritance on FreeBSD).
#	2. After each entry, create a file and a directory there and check
#	   that both the ACL and the block size were inherited.
#	3. Set the extent size on a directory that already has the large ACL.
#	4. Export, import and scrub, then check everything again.
#

. $STF_SUITE/tests/functional/fileblocksize/fileblocksize.kshlib

verify_runnable "global"

typeset fs=$TESTPOOL/$TESTFS/acl
typeset -i entries=48

function cleanup
{
	datasetexists $fs && destroy_dataset $fs
}

#
# Add inheritable ACL entry i (user 5000+i) to a directory.
#
function add_entry # dir i
{
	if is_linux; then
		log_must setfacl -d -m u:$((5000 + $2)):rwx $1
	else
		log_must setfacl -a 0 u:$((5000 + $2)):rwxp:fd:allow $1
	fi
}

#
# Count the inherited user entries (users 5000 and up) on a file.
#
function count_entries # path
{
	getfacl -n $1 2>/dev/null | grep -cE '^ *user:5[0-9]{3}:'
}

function check # dir i
{
	typeset f=$1/f.$2 d=$1/d.$2
	verify_ext $f extsize 8192
	verify_blksz $f 8192
	verify_ext $d extszinherit 8192
	(( $(count_entries $f) >= $2 )) ||
	    log_fail "$f has $(count_entries $f) ACL entries, expected $2"
	(( $(count_entries $d) >= $2 )) ||
	    log_fail "$d has $(count_entries $d) ACL entries, expected $2"
}

log_onexit cleanup
log_assert "Directory inheritance works with large inherited ACLs"

typeset acltype=nfsv4
is_linux && acltype=posix

typeset dnsize i
for dnsize in legacy auto; do
	log_must zfs create -o acltype=$acltype -o xattr=sa \
	    -o aclinherit=passthrough -o dnodesize=$dnsize $fs
	typeset mnt=$(get_prop mountpoint $fs)
	log_must mkdir $mnt/inh $mnt/late
	log_must extsize -s 8192 $mnt/inh

	for i in $(seq 1 $entries); do
		add_entry $mnt/inh $i
		add_entry $mnt/late $i
		write_mib $mnt/inh/f.$i
		log_must mkdir $mnt/inh/d.$i
		check $mnt/inh $i
	done

	# the extent size added to a directory with a large ACL
	log_must extsize -s 8192 $mnt/late
	verify_ext $mnt/late extszinherit 8192
	write_mib $mnt/late/f.$entries
	log_must mkdir $mnt/late/d.$entries
	check $mnt/late $entries

	reimport_pool
	log_must zpool scrub -w $TESTPOOL
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
	for i in $(seq 1 $entries); do
		check $mnt/inh $i
	done
	check $mnt/late $entries
	log_must destroy_dataset $fs
done

log_pass "Directory inheritance works with large inherited ACLs"
