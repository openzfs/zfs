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
# Reporting preserves a failure when a later invocation of the test passes.
#
# STRATEGY:
# 1. Fail a test once, then pass the same pathname in another group or iteration.
# 2. Check that the reporter lists the failure and returns a failure status.
# 3. Check that runs containing only passes still report success.
#

verify_runnable "both"

typeset work
work=$(mktemp -d "$TEST_BASE_DIR/test-runner-report.XXXXXX")
typeset runner="$STF_TOOLS/bin/test-runner.py"
typeset reporter="$STF_TOOLS/bin/zts-report.py"
typeset cases="$work/zfs-tests/tests/functional/test_runner"

function cleanup
{
	log_must rm -rf "$work"
}
log_onexit cleanup

log_assert "A later pass does not hide an earlier failure of the same test."
log_must test -d "$work"
log_must mkdir -p "$cases"

cat > "$cases/result" <<EOF
#!/bin/sh
if [ ! -e "$work/attempted" ]; then
	touch "$work/attempted"
	exit 1
fi
EOF
log_must chmod 755 "$cases/result"

typeset -a selection
for scenario in grouped iterations; do
	log_note "Checking $scenario results."
	cat > "$work/tests.run" <<EOF
[DEFAULT]
timeout = 5

[$cases]
tests = ['result']
EOF
	selection=(-c "$work/tests.run")
	if [[ $scenario == grouped ]]; then
		cat >> "$work/tests.run" <<EOF

[$cases:Platform]
tests = ['result']
EOF
	else
		selection+=(-I 2)
	fi

	log_must rm -f "$work/attempted"
	log_mustnot "$runner" -o "$work/$scenario-first" \
	    "${selection[@]}" > "$work/runner.log"
	log_must sed 's/^/nested-runner: /' "$work/runner.log"
	log_must test "$(awk -v pathname="$cases/result" \
	    'index($0, ": " pathname " (run as ") { print $NF }' \
	    "$work/runner.log")" = $'[FAIL]\n[PASS]'
	"$reporter" "$work/runner.log" > "$work/report"
	status=$?
	log_must sed 's/^/nested-report: /' "$work/report"
	log_must test "$status" -eq 1
	log_must grep -F "FAIL test_runner/result (expected PASS)" "$work/report"

	log_must "$runner" -o "$work/$scenario-passing" \
	    "${selection[@]}" > "$work/runner.log"
	log_must test "$(awk -v pathname="$cases/result" \
	    'index($0, ": " pathname " (run as ") { print $NF }' \
	    "$work/runner.log")" = $'[PASS]\n[PASS]'
	log_must "$reporter" "$work/runner.log"
done

log_pass "A later pass does not hide an earlier failure of the same test."
