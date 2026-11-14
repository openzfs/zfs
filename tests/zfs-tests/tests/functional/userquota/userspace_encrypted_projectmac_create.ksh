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
# zpool create sets the projectmac pool property to the platform default or to
# the value it's given, and a pool's encrypted root dataset, which is created
# before the pool's properties are set, is written in that form.
#
# STRATEGY:
# 1. Create a pool without encryption, and check that projectmac is set to
#    the platform default with source local
# 2. For each of on, off and mixed, create a pool with that value and an
#    encrypted root dataset, and check the property and the root dataset's
#    form, which for mixed is the platform default, after it is created,
#    written, exported and imported
#

verify_runnable "global"

POOLNAME=testpool$$
FILEDEV=$TEST_BASE_DIR/userspace_encrypted_projectmac_create

function cleanup
{
	destroy_pool $POOLNAME
	rm -f $FILEDEV
}

log_onexit cleanup

log_assert "zpool create sets projectmac, and the encrypted root uses it"

truncate -s $SPA_MINDEVSIZE $FILEDEV

# 1.
log_must zpool create $POOLNAME $FILEDEV
form=$(projectmac_default $POOLNAME)
check_pool_projectmac $POOLNAME $form local
log_must zpool destroy $POOLNAME

# 2.
for value in on off mixed; do
	if [[ $value == mixed ]]; then
		expected=$form
		source=default
	else
		expected=$value
		source=local
	fi
	log_must eval "echo password | zpool create -o projectmac=$value" \
	    "-O encryption=on -O keyformat=passphrase -O keylocation=prompt" \
	    "$POOLNAME $FILEDEV"
	check_pool_projectmac $POOLNAME $value $source
	check_projectmac $POOLNAME $expected
	write_and_sync $POOLNAME
	check_projectmac $POOLNAME $expected

	log_must zpool export $POOLNAME
	log_must zpool import -d $TEST_BASE_DIR $POOLNAME
	load_and_mount $POOLNAME
	write_and_sync $POOLNAME
	check_projectmac $POOLNAME $expected
	check_pool_projectmac $POOLNAME $value $source
	log_must zfs projectspace $POOLNAME
	log_must zpool destroy $POOLNAME
done

log_pass "zpool create sets projectmac, and the encrypted root uses it"
