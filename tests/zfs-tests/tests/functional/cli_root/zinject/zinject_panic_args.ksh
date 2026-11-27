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
#	'zinject -p' with an incompatible option names -p in its error.
#
# STRATEGY:
#	1. Run 'zinject -p spa_sync' with -t and then with -f.
#	2. Check that each is rejected with exit status 2, that the error
#	   names the panic option (-p) and not -P, and that no handler was
#	   registered.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

function cleanup
{
	zinject -c all
	rm -f $TEST_BASE_DIR/zinject_panic_args.err
}

log_onexit cleanup

log_assert "zinject -p names itself when given an incompatible option"

typeset err=$TEST_BASE_DIR/zinject_panic_args.err

for opts in "-t data" "-f 50"; do
	zinject -p spa_sync $opts $TESTPOOL 2>$err
	typeset -i rc=$?
	cat $err
	(( rc == 2 )) || log_fail "zinject -p spa_sync $opts: exit $rc, not 2"
	grep -q "panic (-p) incompatible with other options" $err ||
	    log_fail "zinject -p spa_sync $opts: wrong error message"
	grep -q "(-P)" $err &&
	    log_fail "zinject -p spa_sync $opts: error names -P"
done

[[ "$(zinject)" == "No handlers registered."* ]] ||
    log_fail "a handler was registered"

log_pass "zinject -p names itself when given an incompatible option"
