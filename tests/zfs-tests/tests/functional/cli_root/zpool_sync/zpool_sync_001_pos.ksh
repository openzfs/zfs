#!/bin/ksh -p
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#
# Copyright (c) 2017 Datto Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify 'zpool sync' can sync txgs to the pool(s) main vdevs.
#
# STRATEGY:
# 1. Create a pool
# 2. Use zdb to obtain current txg
# 3. Create a file in the pool if we're not using force sync
# 4. Use zpool sync to sync pool
# 5. Verify the new txg is now bigger than the saved one
#

verify_runnable "global"

function get_txg {
	typeset -i txg=$(zdb -u $1 | sed -n 's/^[ 	][ 	]*txg = \(.*\)$/\1/p')
	echo $txg
}

set -A args "sync $TESTPOOL" "sync -f $TESTPOOL" "sync" "sync -f"

log_assert "Verify 'zpool sync' can sync a pool"

typeset -i i=0
typeset -i orig_txg=0
typeset -i new_txg=0
while [[ $i -lt ${#args[*]} ]]; do
	orig_txg=$(get_txg $TESTPOOL)
	if ! [[ "${args[i]}" =~ "-f" ]]; then
		log_must touch /$TESTPOOL/$i
	fi
	log_must zpool ${args[i]}
	new_txg=$(get_txg $TESTPOOL)
	if [[ $orig_txg -ge $new_txg ]]; then
		log_fail "'zpool ${args[i]}' failed: txg $orig_txg >= $new_txg"
	fi
	((i = i + 1))
done

# sync_pool is implemented using 'zpool sync' so let's test it as well

# make sure we can use sync_pool with force sync explicitly not used
orig_txg=$(get_txg $TESTPOOL)
log_must touch /$TESTPOOL/$i
log_must sync_pool $TESTPOOL false
new_txg=$(get_txg $TESTPOOL)
if [[ $orig_txg -ge $new_txg ]]; then
	log_fail "'sync_pool $TESTPOOL false' failed: txg $orig_txg >= $new_txg"
fi

# make sure we can use sync_pool with force sync explicitly enabled
orig_txg=$(get_txg $TESTPOOL)
log_must sync_pool $TESTPOOL true
new_txg=$(get_txg $TESTPOOL)
if [[ $orig_txg -ge $new_txg ]]; then
	log_fail "'sync_pool $TESTPOOL true' failed: txg $orig_txg >= $new_txg"
fi

# make sure we can use sync_pool with force sync implicitly not used
orig_txg=$(get_txg $TESTPOOL)
log_must touch /$TESTPOOL/$i
log_must sync_pool $TESTPOOL
new_txg=$(get_txg $TESTPOOL)
if [[ $orig_txg -ge $new_txg ]]; then
	log_fail "'sync_pool $TESTPOOL' failed: txg $orig_txg >= $new_txg"
fi

log_pass "'zpool sync' syncs pool as expected."
