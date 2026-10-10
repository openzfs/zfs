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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify the formatting of the data error summary and list in 'zpool status'
# text and JSON output, for both root and unprivileged users.
#
# STRATEGY:
# 1. Create a pool with a corrupted file
# 2. Verify 'zpool status -v' prints the file name with no trailing
#    whitespace, and 'zpool status -vv' prints it followed by its ranges
# 3. Verify 'zpool status -j' reports the error count but no error list
# 4. Verify 'zpool status -jv' reports the error count and the error list
# 5. Verify an unprivileged user, who cannot read the error log, still sees
#    the correct error count with 'zpool status' and 'zpool status -j[v]'
#

verify_runnable "global"

ZS_USER="zsuser"
ZS_GROUP="zsgroup"

function cleanup
{
	log_must zinject -c all
	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2
	rm -f $TESTDIR/vdev_a
	del_user $ZS_USER
	del_group $ZS_GROUP
}

# Print the error count from the text 'zpool status' output on stdin.
function text_errcount
{
	awk '/^errors: No known data errors/ {print 0}
	    /^errors: [0-9]+ data errors/ {print $2}'
}

# Print the '.pools.<pool>.<key>' value from the JSON output on stdin.
function json_val # <key>
{
	jq -c --arg p "$TESTPOOL2" --arg k "$1" '.pools[$p][$k]'
}

log_assert "Verify 'zpool status' data error output formatting"
log_onexit cleanup

truncate -s $MINVDEVSIZE $TESTDIR/vdev_a
log_must zpool create -f $TESTPOOL2 $TESTDIR/vdev_a
log_must zfs set compression=off $TESTPOOL2

file=/$TESTPOOL2/file
log_must mkfile 1m $file
log_must zinject -t data -e checksum -f 100 -am $file
log_mustnot dd if=$file of=/dev/null bs=128k
log_must zinject -c all
log_must zpool sync $TESTPOOL2

nerr=$(zpool status $TESTPOOL2 | text_errcount)
log_note "text error count: $nerr"
if [[ -z "$nerr" ]] || (( nerr == 0 )); then
	log_fail "No data errors reported in 'zpool status $TESTPOOL2'"
fi

# The file name must be the last thing on its line with -v ...
log_must eval "zpool status -v $TESTPOOL2 | grep -q '^[[:space:]]*$file\$'"
log_mustnot eval "zpool status -v $TESTPOOL2 | grep -q '${file}[[:space:]]\$'"
# ... and be followed by its byte ranges with -vv.
log_must eval "zpool status -vv $TESTPOOL2 | grep -q '^[[:space:]]*$file [0-9]'"

# -j always reports the error count, but only includes the list with -v.
out=$(zpool status -jp --json-int $TESTPOOL2)
log_note "zpool status -j: $(echo "$out" | json_val error_count)"
[[ "$(echo "$out" | json_val error_count)" == "$nerr" ]] || \
	log_fail "'zpool status -j' error_count is not $nerr"
[[ "$(echo "$out" | json_val errlist)" == "null" ]] || \
	log_fail "'zpool status -j' unexpectedly includes errlist"

out=$(zpool status -jvp --json-int $TESTPOOL2)
[[ "$(echo "$out" | json_val error_count)" == "$nerr" ]] || \
	log_fail "'zpool status -jv' error_count is not $nerr"
[[ "$(echo "$out" | json_val errlist)" == "[\"$file\"]" ]] || \
	log_fail "'zpool status -jv' errlist is not [\"$file\"]"

#
# An unprivileged user can run 'zpool status' but is not allowed to read
# the error log.  The error count must still be reported correctly.
#
log_must add_group $ZS_GROUP
log_must add_user $ZS_GROUP $ZS_USER

if ! user_run $ZS_USER zpool status $TESTPOOL2 ; then
	log_note "$ZS_USER cannot run zpool status, skipping unprivileged checks"
	log_pass "'zpool status' data error output formatting is correct"
fi
[[ "$(text_errcount < $TEST_BASE_DIR/out)" == "$nerr" ]] || \
	log_fail "Unprivileged 'zpool status' error count is not $nerr"

for flags in "-jp --json-int" "-jvp --json-int"; do
	log_must user_run $ZS_USER zpool status $flags $TESTPOOL2
	[[ "$(json_val error_count < $TEST_BASE_DIR/out)" == "$nerr" ]] || \
		log_fail "Unprivileged 'zpool status $flags' error_count" \
		    "is not $nerr"
	[[ "$(json_val errlist < $TEST_BASE_DIR/out)" == "null" ]] || \
		log_fail "Unprivileged 'zpool status $flags' includes errlist"
done

log_pass "'zpool status' data error output formatting is correct"
