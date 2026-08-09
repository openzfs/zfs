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
# Copyright (c) 2025 by Lawrence Livermore National Security, LLC.
#

# DESCRIPTION:
# Verify ZED synchronous zedlets work as expected
#
# STRATEGY:
# 1. Create a scrub_start zedlet that runs quickly
# 2. Create a scrub_start zedlet that runs slowly (takes seconds)
# 3. Create a scrub_finish zedlet that is synchronous and runs slowly
# 4. Create a trim_start zedlet that runs quickly
# 4. Scrub the pool
# 5. Trim the pool
# 6. Verify the synchronous scrub_finish zedlet waited for the scrub_start
#    zedlets to finish (including the slow one).  If the scrub_finish zedlet
#    was not synchronous, it would have completed before the slow scrub_start
#    zedlet.
# 7. Verify the trim_start zedlet waited for the slow synchronous scrub_finish
#    zedlet to complete.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/events/events_common.kshlib

verify_runnable "both"

OUR_ZEDLETS="scrub_start-async.sh scrub_start-slow.sh scrub_finish-sync-slow.sh trim_start-async.sh"

OUTFILE="$TEST_BASE_DIR/zed_synchronous_zedlet_lines"
TESTPOOL2=testpool2

function cleanup
{
	zed_stop

	for i in $OUR_ZEDLETS ; do
		log_must rm -f $ZEDLET_DIR/$i
	done
	destroy_pool $TESTPOOL2
	log_must rm -f $TEST_BASE_DIR/vdev-file-sync-zedlet
	log_must rm -f $OUTFILE
}

log_assert "Verify ZED synchronous zedlets work as expected"

log_onexit cleanup

# Make a pool
log_must truncate -s 100M $TEST_BASE_DIR/vdev-file-sync-zedlet
log_must zpool create $TESTPOOL2 $TEST_BASE_DIR/vdev-file-sync-zedlet

# Do an initial scrub
log_must zpool scrub -w $TESTPOOL2

log_must zpool events -c

mkdir -p $ZEDLET_DIR

# Create zedlets
cat << EOF > $ZEDLET_DIR/scrub_start-async.sh
#!/bin/ksh -p
echo "\$(date) \$(basename \$0)"  >> $OUTFILE
EOF

cat << EOF > $ZEDLET_DIR/scrub_start-slow.sh
#!/bin/ksh -p
sleep 3
echo "\$(date) \$(basename \$0)"  >> $OUTFILE
EOF

cat << EOF > $ZEDLET_DIR/scrub_finish-sync-slow.sh
#!/bin/ksh -p
sleep 3
echo "\$(date) \$(basename \$0)"  >> $OUTFILE
EOF

cat << EOF > $ZEDLET_DIR/trim_start-async.sh
#!/bin/ksh -p
echo "\$(date) \$(basename \$0)"  >> $OUTFILE
EOF

for i in $OUR_ZEDLETS ; do
	log_must chmod +x $ZEDLET_DIR/$i
done

log_must zed_start

# Do a scrub - it should be instantaneous.
log_must zpool scrub -w $TESTPOOL2

# Start off a trim immediately after scrubbing.  The trim should be
# instantaneous and generate a trim_start event.  This will happen in parallel
# with the slow 'scrub_finish-sync-slow.sh' zedlet still running.
log_must zpool trim -w $TESTPOOL2

# Wait for scrub_finish event to happen for sanity. This is the *event*, not
# the completion of zedlets for the event.
log_must file_wait_event $ZED_DEBUG_LOG 'sysevent\.fs\.zfs\.scrub_finish' 10

# At a minimum, scrub_start-slow.sh + scrub_finish-sync-slow.sh will take a
# total of 6 seconds to run, so wait 7 sec to be sure.
sleep 7

# If our zedlets were run in the right order, with sync correctly honored, you
# will see this ordering in $OUTFILE:
#
# Fri May 16 12:04:23 PDT 2025 scrub_start-async.sh
# Fri May 16 12:04:26 PDT 2025 scrub_start-slow.sh
# Fri May 16 12:04:31 PDT 2025 scrub_finish-sync-slow.sh
# Fri May 16 12:04:31 PDT 2025 trim_start-async.sh
#
# Check for this ordering

# Get a list of just the script names in the order they were executed
# from OUTFILE
lines="$(echo $(grep -Eo '(scrub|trim)_.+\.sh$' $OUTFILE))"

# Compare it to the ordering we expect
expected="\
scrub_start-async.sh \
scrub_start-slow.sh \
scrub_finish-sync-slow.sh \
trim_start-async.sh"
log_must test "$lines" == "$expected"

log_pass "Verified synchronous zedlets"
