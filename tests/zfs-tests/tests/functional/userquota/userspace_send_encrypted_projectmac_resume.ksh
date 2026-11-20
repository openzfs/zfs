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
# An interrupted raw receive isn't converted when the projectmac pool
# property is set, and once resumed, the received dataset takes the form the
# property sets.
#
# STRATEGY:
# 1. Create, write and snapshot an encrypted filesystem in the platform
#    default form, and save a raw send of the snapshot
# 2. Receive half of the stream resumably, so that the receive fails and
#    leaves a resume token
# 3. Set projectmac to the other form, then resume the receive
# 4. Load the received dataset's key, mount it, and check that it shows and
#    is written in the other form, and has the same contents as its source
# 5. Export and import the pool, and check that the forms were kept
#

verify_runnable "global"

POOLNAME=testpool$$
FILEDEV=$TEST_BASE_DIR/userspace_send_encrypted_projectmac_resume
STREAM=$TEST_BASE_DIR/userspace_send_encrypted_projectmac_resume.stream
SRC=$POOLNAME/src
DST=$POOLNAME/dst

function cleanup
{
	destroy_pool $POOLNAME
	rm -f $FILEDEV $STREAM
}

log_onexit cleanup

log_assert "A resumed raw receive takes the form projectmac sets"

truncate -s 256m $FILEDEV
log_must zpool create $POOLNAME $FILEDEV
form=$(projectmac_default $POOLNAME)
other=$(projectmac_other $form)

# 1.
create_encrypted $SRC
log_must dd if=/dev/urandom of=$(get_prop mountpoint $SRC)/big bs=1024k \
    count=8
log_must zfs snapshot $SRC@snap
check_projectmac $SRC@snap $form
log_must eval "zfs send -w $SRC@snap > $STREAM"

# 2.
typeset -i size=$(wc -c < $STREAM)
log_must truncate -s $(( size / 2 )) $STREAM
log_mustnot eval "zfs recv -s $DST < $STREAM"
token=$(get_prop receive_resume_token $DST)
[[ -n $token && $token != "-" ]] || log_fail "$DST has no resume token"

# 3.
log_must zpool set projectmac=$other $POOLNAME
check_projectmac $SRC $other
log_must eval "zfs send -t $token | zfs recv -s $DST"

# 4.
load_and_mount $DST
check_projectmac $DST $other
write_and_sync $DST
check_projectmac $DST $other
log_must cmp $(get_prop mountpoint $SRC)/big $(get_prop mountpoint $DST)/big

# 5.
log_must zpool export $POOLNAME
log_must zpool import -d $TEST_BASE_DIR $POOLNAME
for fs in $SRC $DST; do
	load_and_mount $fs
	write_and_sync $fs
	check_projectmac $fs $other
	log_must zfs projectspace $fs
done
check_projectmac $SRC@snap $form

log_pass "A resumed raw receive takes the form projectmac sets"
