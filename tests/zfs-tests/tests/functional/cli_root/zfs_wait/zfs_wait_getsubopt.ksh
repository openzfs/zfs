#!/bin/ksh -p
# SPDX-License-Identifier: 0BSD

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# zfs wait -t used to accept getsubopt(3)-style deleteq=whatever;
# it doesn't anymore
#

log_mustnot zfs wait -t deleteq=getsubopt $TESTPOOL

log_pass "'zfs wait -t' doesn't accept =getsubopt suffixes."
