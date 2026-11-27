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
# DESCRIPTION:
# An import or export delay ('zinject -P import|export') takes effect
# when other zinject handlers were registered before it.
#
# STRATEGY:
# 1. Create two pools, and register a handler of another type (a device
#    fault that never fires) on the first.
# 2. Add an import delay for the exported second pool, and check that
#    the import takes at least that long and clears the handler.
# 3. Do the same for an export delay.
# 4. With only pool delays registered, add an export delay for the first
#    pool, then an import delay for the exported second pool.  Check that
#    the second pool's import takes the delay and clears its handler,
#    that its export doesn't take the first pool's delay, and that the
#    first pool's export delay is still registered.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

typeset VDEV1=$TEST_BASE_DIR/zinject_pool_delay.1
typeset VDEV2=$TEST_BASE_DIR/zinject_pool_delay.2
typeset POOL1=zinject_delay_1
typeset POOL2=zinject_delay_2
typeset -i DELAY=5

function cleanup
{
	zinject -c all
	destroy_pool $POOL1
	poolexists $POOL2 || zpool import -d $TEST_BASE_DIR $POOL2
	destroy_pool $POOL2
	rm -f $VDEV1 $VDEV2
}

log_onexit cleanup

log_assert "Import and export delays work with other handlers registered"

log_must truncate -s $MINVDEVSIZE $VDEV1 $VDEV2
log_must zpool create $POOL1 $VDEV1
log_must zpool create $POOL2 $VDEV2

# A handler of another type, registered first.  -f 0.001 keeps it
# from disturbing the pool.
log_must zinject -d $VDEV1 -e io -T read -f 0.001 $POOL1

log_must zpool export $POOL2
log_must zinject -P import -s $DELAY $POOL2
SECONDS=0
log_must zpool import -d $TEST_BASE_DIR $POOL2
typeset -i took=$SECONDS
log_note "import took $took s with a $DELAY s import delay"
(( took >= DELAY )) || log_fail "import took $took s, under the $DELAY s delay"
zinject | grep -q "$POOL2" && log_fail "import delay handler not cleared"

log_must zinject -P export -s $DELAY $POOL2
SECONDS=0
log_must zpool export $POOL2
took=$SECONDS
log_note "export took $took s with a $DELAY s export delay"
(( took >= DELAY )) || log_fail "export took $took s, under the $DELAY s delay"
zinject | grep -q "$POOL2" && log_fail "export delay handler not cleared"

# Another pool's export delay, registered ahead of the import delay.
log_must zinject -c all
log_must zinject -P export -s $DELAY $POOL1
log_must zinject -P import -s $DELAY $POOL2
SECONDS=0
log_must zpool import -d $TEST_BASE_DIR $POOL2
took=$SECONDS
log_note "import took $took s with $POOL1's export delay registered first"
(( took >= DELAY )) || log_fail "import took $took s, under the $DELAY s delay"
zinject | grep -q "$POOL2" && log_fail "import delay handler not cleared"

# The first pool's export delay must not apply to the second pool.
SECONDS=0
log_must zpool export $POOL2
took=$SECONDS
log_note "export of $POOL2 took $took s with $POOL1's export delay registered"
(( took < DELAY )) || log_fail "export of $POOL2 took $POOL1's export delay"

typeset handler_id=$(zinject | awk -v pool=$POOL1 \
    '$2 == pool && $4 == "export" { print $1 }')
[[ -n "$handler_id" ]] || log_fail "$POOL1's export delay handler was removed"
log_must zinject -c $handler_id
zinject | grep -q "$POOL1" && log_fail "$POOL1's export delay not cleared"

log_pass "Import and export delays work with other handlers registered"
