#!/bin/ksh -p
# SPDX-License-Identifier: 0BSD

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# zfs list -t used to accept getsubopt(3)-style filesystem=whatever;
# it doesn't anymore
#

log_mustnot zfs list -t filesystem=getsubopt

log_pass "'zfs list -t' doesn't accept =getsubopt suffixes."
