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
# Selected tests must pass verification before any test executes.
#
# STRATEGY:
# 1. Run valid tests, including an explicit skip, with an invalid group excluded
#    by its tag.
# 2. Reject missing and non-executable selected tests before executing any test.
# 3. Check explicitly named files and executable-only directory discovery.
#

verify_runnable "both"

typeset work
work=$(mktemp -d "$TEST_BASE_DIR/test-runner.XXXXXX")
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

log_assert "Selected tests pass verification before any test executes."
log_must test -d "$work"
log_must mkdir "$work/cases"

cat > "$work/cases/pass" <<EOF
#!/bin/sh
touch "$work/executed"
EOF
printf '#!/bin/sh\nexit 4\n' > "$work/cases/skip"
printf '#!/bin/sh\nexit 0\n' > "$work/cases/failsafe"
printf '#!/bin/sh\nexit 0\n' > "$work/cases/nonexec"
log_must chmod 755 "$work/cases/pass" "$work/cases/skip" \
    "$work/cases/failsafe"
log_must chmod 644 "$work/cases/nonexec"

for name in skip missing nonexec; do
	cat > "$work/tests.run" <<EOF
[DEFAULT]
timeout = 5

[$work/cases]
tests = ['pass', '$name']
tags = ['selected']

[$work/ignored]
tests = ['absent']
tags = ['excluded']
EOF
	if [[ $name == skip ]]; then
		log_must run_runner -o "$work/$name" -c "$work/tests.run" \
		    -T selected
		log_must test -f "$work/executed"
		log_must rm "$work/executed"
	else
		log_mustnot run_runner -o "$work/$name" \
		    -c "$work/tests.run" -T selected
		log_must test ! -e "$work/executed"
	fi
done

log_mustnot run_runner -o "$work/direct" "$work/cases/pass" \
    "$work/cases/nonexec"
log_must test ! -e "$work/executed"

log_must run_runner -o "$work/discovery" -g -s failsafe "$work/cases"
log_must test -f "$work/executed"

log_pass "Selected tests pass verification before any test executes."
