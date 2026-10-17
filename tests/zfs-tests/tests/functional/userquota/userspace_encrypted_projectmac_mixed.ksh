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
# With the projectmac pool property mixed, an encrypted filesystem keeps the
# form it has, one with no form yet takes the platform default, and a clone
# keeps the form of its origin.
#
# STRATEGY:
# 1. With the platform default set, create an encrypted filesystem PARENT
#    and a child SRC that inherits its key; set the other form, which
#    converts both, snapshot SRC, and set mixed, and check that the pool
#    history logged both
# 2. Create a child that inherits PARENT's key and a new encryption root
#    under PARENT, and check that they take the platform default
# 3. Clone SRC's snapshot under PARENT, and check that the clone keeps the
#    snapshot's form, before and after it is written
# 4. Raw receive SRC's snapshot under PARENT, and check that after it is
#    written it has the platform default, while its snapshot, whose local
#    MAC is zeroed, has no form
# 5. Check that nothing set projectmac, then export and import the pool, and
#    check that the forms were kept
#

verify_runnable "global"

POOLNAME=testpool$$
FILEDEV=$TEST_BASE_DIR/userspace_encrypted_projectmac_mixed
PARENT=$POOLNAME/parent
SRC=$PARENT/src
CHILD=$PARENT/child
ROOT=$PARENT/root
CLONE=$PARENT/clone
RECV=$PARENT/recv

function cleanup
{
	destroy_pool $POOLNAME
	rm -f $FILEDEV
}

log_onexit cleanup

log_assert "With projectmac=mixed, datasets keep their form or take the default"

truncate -s 256m $FILEDEV
log_must zpool create $POOLNAME $FILEDEV
form=$(projectmac_default $POOLNAME)
other=$(projectmac_other $form)

# 1.
create_encrypted $PARENT
log_must zfs create $SRC
write_and_sync $SRC
log_must zpool set projectmac=$other $POOLNAME
log_must zfs snapshot $SRC@snap
log_must zpool set projectmac=mixed $POOLNAME
check_pool_projectmac $POOLNAME mixed default
for value in $other mixed; do
	log_must eval "zpool history -i $POOLNAME | grep -q 'projectmac=$value'"
done
nsets=$(zpool history -i $POOLNAME | grep -c 'projectmac=')
check_projectmac $PARENT $other
check_projectmac $SRC $other
check_projectmac $SRC@snap $other

# 2.
log_must zfs create $CHILD
write_and_sync $CHILD
check_projectmac $CHILD $form
create_encrypted $ROOT
check_projectmac $ROOT $form

# 3.
log_must zfs clone $SRC@snap $CLONE
check_projectmac $CLONE $other
write_and_sync $CLONE
check_projectmac $CLONE $other

# 4.
log_must eval "zfs send -w $SRC@snap | zfs recv $RECV"
load_and_mount $RECV
write_and_sync $RECV
check_projectmac $RECV $form
check_projectmac $RECV@snap -

# 5.
check_pool_projectmac $POOLNAME mixed default
log_must zpool export $POOLNAME
log_must zpool import -d $TEST_BASE_DIR $POOLNAME
for fs in $PARENT $ROOT $RECV; do
	log_must eval "echo password | zfs load-key $fs"
done
log_must zfs mount -a
for fs in $PARENT $SRC $CLONE; do
	write_and_sync $fs
	check_projectmac $fs $other
done
for fs in $CHILD $ROOT $RECV; do
	write_and_sync $fs
	check_projectmac $fs $form
done
check_pool_projectmac $POOLNAME mixed default
(( $(zpool history -i $POOLNAME | grep -c 'projectmac=') == nsets )) || \
	log_fail "projectmac was set implicitly"

log_pass "With projectmac=mixed, datasets keep their form or take the default"
