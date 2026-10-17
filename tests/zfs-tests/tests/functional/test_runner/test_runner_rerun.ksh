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

. "$STF_SUITE/include/libtest.shlib"

#
# DESCRIPTION:
# Rerunning a failure log selects only the failed test and its group.
#
# STRATEGY:
# 1. Run passing and failing tests in two groups sharing the same directory.
# 2. Rerun failures from the emitted log, retaining the group identifier.
# 3. Check standalone tests and both timestamped and older result lines.
#

verify_runnable "both"

typeset work
work=$(mktemp -d "$TEST_BASE_DIR/test-runner-rerun.XXXXXX")
typeset runner="$STF_TOOLS/bin/test-runner.py"

function cleanup
{
	log_must rm -rf "$work"
}
log_onexit cleanup

# Keep nested records out of the outer runner's debug stream.
function run_runner
{
	"$runner" "$@" > "$work/runner.log" 2>&1
	typeset status=$?
	log_must sed 's/^/nested-runner: /' "$work/runner.log"
	return "$status"
}

log_assert "Failure-log reruns select only the failed test and its group."
log_must test -d "$work"
log_must mkdir "$work/cases"

cat > "$work/cases/pass" <<EOF
#!/bin/sh
echo pass >> "$work/executed"
EOF
cat > "$work/cases/fail" <<EOF
#!/bin/sh
echo fail >> "$work/executed"
exit 1
EOF
log_must chmod 755 "$work/cases/pass" "$work/cases/fail"

typeset -a selection
for scenario in common labelled standalone; do
	common=pass
	labelled=fail
	expected=$'pass\nfail'
	if [[ $scenario == common ]]; then
		common=fail
		labelled=pass
	fi
	cat > "$work/tests.run" <<EOF
[DEFAULT]
timeout = 5
pre = pass

[$work/cases]
tests = ['$common']

[$work/cases:Platform]
tests = ['$labelled']
EOF
	selection=(-c "$work/tests.run")
	if [[ $scenario == standalone ]]; then
		selection=("$work/cases/pass" "$work/cases/fail")
		expected=fail
	fi
	log_mustnot run_runner -o "$work/$scenario-first" "${selection[@]}"
	log_must sed -E 's/^\[[^]]+\] //' \
	    "$work/$scenario-first/current/log" > "$work/legacy.log"

	for format in current legacy; do
		logfile="$work/$scenario-first/current/log"
		[[ $format == legacy ]] && logfile="$work/legacy.log"
		log_must rm "$work/executed"
		log_mustnot run_runner -o "$work/$scenario-$format" \
		    "${selection[@]}" -l "$logfile"
		log_must test -f "$work/executed"
		log_must test "$(cat "$work/executed")" = "$expected"
	done
done

log_pass "Failure-log reruns select only the failed test and its group."
