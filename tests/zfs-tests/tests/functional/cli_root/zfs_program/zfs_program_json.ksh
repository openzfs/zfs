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
# source.  A copy is of the CDDL is also available via the Internet
# at http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2018 Datto Inc.
# Copyright (c) 2019 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# STRATEGY:
#	1. Compare JSON output formatting for a channel program to template
#	2. Using bad command line option (-Z) gives correct error output
#

verify_runnable "both"

function cleanup
{
	log_must zfs destroy $TESTDS
	return 0
}
log_onexit cleanup

log_assert "Channel programs output valid JSON"

TESTDS="$TESTPOOL/zcp-json"
log_must zfs create $TESTDS

TESTZCP="/$TESTDS/zfs_rlist.zcp"
cat > "$TESTZCP" << EOF
	succeeded = {}
	failed = {}

	function list_recursive(root, prop)
		for child in zfs.list.children(root) do
			list_recursive(child, prop)
		end
		val, src  = zfs.get_prop(root, prop)
		if (val == nil) then
			failed[root] = val
		else
			succeeded[root] = val
		end
	end

	args = ...

	argv = args["argv"]

	list_recursive(argv[1], argv[2])

	results = {}
	results["succeeded"] = succeeded
	results["failed"] = failed
	return results
EOF

# 1. Compare JSON output formatting for a channel program to template
typeset -a pos_cmds=("recordsize" "type")
typeset -a pos_cmds_out=(
"{
    \"return\": {
        \"failed\": {},
        \"succeeded\": {
            \"$TESTDS\": 131072
        }
    }
}"
"{
    \"return\": {
        \"failed\": {},
        \"succeeded\": {
            \"$TESTDS\": \"filesystem\"
        }
    }
}")

typeset -i cnt=0
typeset cmd
for cmd in ${pos_cmds[@]}; do
	log_must zfs program $TESTPOOL $TESTZCP $TESTDS $cmd 2>&1
	log_must zfs program -j $TESTPOOL $TESTZCP $TESTDS $cmd 2>&1
	OUTPUT=$(zfs program -j $TESTPOOL $TESTZCP $TESTDS $cmd 2>&1 |
	    python3 -m json.tool --sort-keys)
	if [ "$OUTPUT" != "${pos_cmds_out[$cnt]}" ]; then
		log_note "Got     :$OUTPUT"
		log_note "Expected:${pos_cmds_out[$cnt]}"
		log_fail "Unexpected channel program output";
	fi
	cnt=$((cnt + 1))
done

# 2. Using bad command line option (-Z) gives correct error output
typeset -a neg_cmds=("-Z")
typeset -a neg_cmds_out=(
"invalid option 'Z'
usage:
	program [-jn] [-t <instruction limit>] [-m <memory limit (b)>]
	    <pool> <program file> [lua args...]

For the property list, run: zfs set|get

For the delegated permission list, run: zfs allow|unallow

For further help on a command or topic, run: zfs help [<topic>]")
cnt=0
for cmd in ${neg_cmds[@]}; do
	log_mustnot zfs program $cmd $TESTPOOL $TESTZCP $TESTDS 2>&1
	log_mustnot zfs program -j $cmd $TESTPOOL $TESTZCP $TESTDS 2>&1
	OUTPUT=$(zfs program -j $cmd $TESTPOOL $TESTZCP $TESTDS 2>&1)
	if [ "$OUTPUT" != "${neg_cmds_out[$cnt]}" ]; then
		log_note "Got     :$OUTPUT"
		log_note "Expected:${neg_cmds_out[$cnt]}"
		log_fail "Unexpected channel program error output";
	fi
	cnt=$((cnt + 1))
done

log_pass "Channel programs output valid JSON"
