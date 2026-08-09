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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

typeset -i nzvols=1000
typeset -i parallel=$(( $(get_num_cpus) * 2 ))

function cleanup {
  for zvol in $(zfs list -Ho name -t vol) ; do
    log_must_busy zfs destroy $zvol
  done
}

log_onexit cleanup

log_assert "stress test concurrent zvol create/destroy"

function destroy_zvols_until {
  typeset cond=$1
  while true ; do
    IFS='' zfs list -Ho name -t vol | read -r -d '' zvols
    if [[ -n $zvols ]] ; then
      echo $zvols | xargs -n 1 -P $parallel zfs destroy
    fi
    if ! $cond ; then
      break
    fi
  done
}

( seq $nzvols | \
    xargs -P $parallel -I % zfs create -s -V 1G $TESTPOOL/testvol% ) &
cpid=$!
sleep 1

destroy_zvols_until "kill -0 $cpid"
destroy_zvols_until "false"

log_pass "stress test done"
