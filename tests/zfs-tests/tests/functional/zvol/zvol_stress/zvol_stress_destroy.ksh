#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
