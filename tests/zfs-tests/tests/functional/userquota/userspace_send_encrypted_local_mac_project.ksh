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

. $STF_SUITE/tests/functional/userquota/projectmac.kshlib

#
# DESCRIPTION:
# A raw receive zeroes the local MAC, which leaves no sign of its form, so
# the received dataset takes the form set by the projectmac pool property,
# both when it's new and when it's sent back to its origin.
#
# STRATEGY:
# For each value of projectmac, in a new pool:
# 1. With the other value set, create an encrypted source and snapshot it
# 2. Raw send the snapshot to a new target, then set the value under test
# 3. Load the target's key, check that it shows the form it will be written
#    in, then write to it and snapshot it
# 4. Raw send the new snapshot back to the source, then mount and write to
#    the source
# 5. Export and import the pool, then mount both filesystems, and check that
#    each has the form set by the property and that its accounting works
#

verify_runnable "global"

POOLNAME=testpool$$
FILEDEV=$TEST_BASE_DIR/userspace_send_encrypted_local_mac_project
SOURCE=$POOLNAME/source
TARGET=$POOLNAME/target

function cleanup
{
	destroy_pool $POOLNAME
	rm -f $FILEDEV
}

log_onexit cleanup

log_assert "Raw-received datasets take the form set by the pool property"

typeset -A other=([on]=off [off]=on)
for value in on off; do
	truncate -s $SPA_MINDEVSIZE $FILEDEV
	log_must zpool create $POOLNAME $FILEDEV

	# 1.
	log_must zpool set projectmac=${other[$value]} $POOLNAME
	log_must eval "echo password | zfs create -o encryption=on" \
	    "-o keyformat=passphrase -o keylocation=prompt $SOURCE"
	write_and_sync $SOURCE
	log_must zfs snapshot $SOURCE@base
	check_projectmac $SOURCE@base ${other[$value]}

	# 2.
	log_must eval "zfs send -w $SOURCE@base | zfs recv $TARGET"
	log_must zfs umount $SOURCE
	log_must zfs unload-key $SOURCE
	log_must zpool set projectmac=$value $POOLNAME

	# 3.
	load_and_mount $TARGET
	check_projectmac $TARGET $value
	write_and_sync $TARGET
	check_projectmac $TARGET $value
	log_must zfs snapshot $TARGET@s1

	# 4.
	log_must eval "zfs send -w -i @base $TARGET@s1 | zfs recv -F $SOURCE"
	load_and_mount $SOURCE
	write_and_sync $SOURCE
	check_projectmac $SOURCE $value

	# 5.
	log_must zpool export $POOLNAME
	log_must zpool import -d $TEST_BASE_DIR $POOLNAME
	for fs in $SOURCE $TARGET; do
		load_and_mount $fs
		check_projectmac $fs $value
		log_must zfs userspace $fs
		log_must zfs projectspace $fs
	done
	log_must zpool destroy $POOLNAME
	log_must rm -f $FILEDEV
done

log_pass "Raw-received datasets take the form set by the pool property"
