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
# Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# A raw send with a dataset with Chacha20-Poly1305 encryption should only be
# received when the feature is enabled, otherwise rejected. An AES-GCM dataset
# however should always be received.
#

verify_runnable "both"

log_assert "Raw sends with Chacha20-Poly1305 can only be" \
	"recieved if feature available"

function cleanup
{
	poolexists srcpool && destroy_pool srcpool
	poolexists dstpool && destroy_pool dstpool
	log_must rm -f $TESTDIR/srcdev $TESTDIR/dstdev
}

log_onexit cleanup

# create a pool with the the chapoly feature enabled
truncate -s $MINVDEVSIZE $TESTDIR/srcdev
log_must zpool create -f -o feature@chacha20_poly1305=enabled \
	srcpool $TESTDIR/srcdev

# created encrypted filesystems
echo 'password' | create_dataset srcpool/chapoly \
	-o encryption=chacha20-poly1305 -o keyformat=passphrase
echo 'password' | create_dataset srcpool/aesgcm \
	-o encryption=aes-256-gcm -o keyformat=passphrase

# snapshot everything
log_must zfs snapshot -r srcpool@snap


# create a pool with the chapoly feature enabled
truncate -s $MINVDEVSIZE $TESTDIR/dstdev
log_must zpool create -f -o feature@chacha20_poly1305=enabled \
	dstpool $TESTDIR/dstdev

# send and receive both filesystems
log_must eval \
	"zfs send -Rw srcpool/chapoly@snap | zfs receive -u dstpool/chapoly"
log_must eval \
	"zfs send -Rw srcpool/aesgcm@snap | zfs receive -u dstpool/aesgcm"

# destroy received datasets
log_must zfs destroy -r dstpool/chapoly
log_must zfs destroy -r dstpool/aesgcm

# send and receive entire recursive stream
log_must eval "zfs send -Rw srcpool@snap | zfs receive -u dstpool/all"


# remake the dest pool with the chapoly feature disabled
destroy_pool dstpool
log_must zpool create -f -o feature@chacha20_poly1305=disabled \
	dstpool $TESTDIR/dstdev

# send and receive both filesystems. chapoly shoud fail, but aesgcm should
# succeed
log_mustnot eval \
	"zfs send -Rw srcpool/chapoly@snap | zfs receive -u dstpool/chapoly"
log_must eval \
	"zfs send -Rw srcpool/aesgcm@snap | zfs receive -u dstpool/aesgcm"

# destroy received dataset
log_must zfs destroy -r dstpool/aesgcm


# send and receive entire recursive stream, which should fail
log_mustnot eval "zfs send -Rw srcpool@snap | zfs receive -u dstpool/all"


log_pass "Chacha20-Poly1305 datasets can only be recieved" \
	"if the feature is enabled"
