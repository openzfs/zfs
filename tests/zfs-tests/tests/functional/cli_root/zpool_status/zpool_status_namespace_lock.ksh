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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 	Test out that zpool status|get|list are not affected by the
# 	'spa_namespace_lock' being held.  This was an issue in the past
# 	where a pool would hang while holding the lock, and 'zpool status'
# 	would hang waiting for the lock.
#
# STRATEGY:
#	1. Create a test pool.  It should be created quickly.
#	2. Set SPA_NAMESPACE_DELAY_MS to 1 second.
#	3. Create another test pool.  It should take over 1 second due to the
#	   delay.
#	4. Run zpool status|get|list.  They should all run quickly.
#	5. In the background, destroy one of the pools and export the other one
#	   at the same time.  This should cause a lot of lock contention.  Note
#	   that the 1 sec delay is still in effect.
#	6. At the same time run some zpool commands and verify they still run in
#	   200ms or less, and without error.

verify_runnable "global"

FILEDEV1="$TEST_BASE_DIR/filedev1.$$"
FILEDEV2="$TEST_BASE_DIR/filedev2.$$"

log_must truncate -s 100M "$FILEDEV1" "$FILEDEV2"
TESTPOOL1=testpool1
TESTPOOL2=testpool2

function cleanup
{
	restore_tunable SPA_NAMESPACE_DELAY_MS
	datasetexists $TESTPOOL1 && log_must zpool destroy $TESTPOOL1
	datasetexists $TESTPOOL2 && log_must zpool destroy $TESTPOOL2
	rm -f $FILEDEV1 $FILEDEV2

	log_note "debug: $(cat /tmp/out)"
}

# Run a command and test how long it takes.  Fail the test if the command
# fails or takes the wrong amount of time to run.
#
# The arguments consist of an array like this:
#
#	 zpool get all -le 200
#
# The first part of the array is the command and its arguments.
# The 2nd to last argument is a ksh comparator operation like "-le" or "-ge"
# The last argument is the expected time in milliseconds for the comparator.
#
# So the example above reads as "run 'zpool get all' and check that it runs in
# 200 milliseconds or less".
#
function timetest_cmd
{
	# Run the command and record 'real' time.  dec will be a factional value
	# like '1.05' representing the number of seconds it took to run.
	array=($@)

	# Save the last two arguments and remove them from the array
	target_ms="${array[@]: -1}"
	unset array[-1]
	comp="${array[@]: -1}"
	unset array[-1]

	out="$({ time -p "${array[@]}" 1> /dev/null; } 2>&1)"
	rc=$?
	dec=$(echo "$out" | awk '/^real /{print $2}')

	# Calculate milliseconds.  The divide by one at the end removes the
	# decimal places.
	ms=$(bc <<< "scale=0; $dec * 1000 / 1")

	log_note "${array[@]}: $ms $comp $target_ms"

	# For debugging failures
	echo "$@: dec=$dec; rc=$rc; ms=$ms; $out" >> /tmp/out2
	if [ -z "$ms" ] || [ $rc != 0 ] ; then
		log_fail "Bad value: ms=$ms, rc=$rc: $out"
	fi

	if eval ! [ $ms $comp $target_ms ] ; then
		log_fail "Expected time wrong: $ms $comp $target_ms"
	fi
}

log_assert "Verify spa_namespace_lock isn't held by zpool status|get|list"

log_onexit cleanup

# A normal zpool create should take 200ms or less
timetest_cmd zpool create $TESTPOOL1 $FILEDEV1 -le 200

log_must save_tunable SPA_NAMESPACE_DELAY_MS
log_must set_tunable32 SPA_NAMESPACE_DELAY_MS 1000

# We added 1 sec hold time on spa_namespace lock.  zpool create
# should now take at least 1000ms.
timetest_cmd zpool create $TESTPOOL2 $FILEDEV2 -ge 1000

# zpool status|get|list should not take spa_namespace_lock, so they should run
# quickly, even loaded down with options.
timetest_cmd zpool status -pPstvLD -le 200
timetest_cmd zpool get all -le 200
timetest_cmd zpool list -gLPv -le 200

# In the background, destroy one of the pools and export the other one at the
# same time.  This should cause a lot of lock contention.  At the same time
# run some zpool commands and verify they still run in 200ms or less, and
# without error.
zpool destroy $TESTPOOL1 &
zpool export $TESTPOOL2 &
for i in 1 2 3 4 ; do
	timetest_cmd zpool status -pPstvLD -le 200
	timetest_cmd zpool get all -le 200
	timetest_cmd zpool list -gLPv -le 200
	sleep 0.1
done
wait

log_pass "zpool status|get|list was not delayed by the spa_namespace_lock"
