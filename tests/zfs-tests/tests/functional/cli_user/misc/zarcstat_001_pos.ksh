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
# Copyright (c) 2015 by Lawrence Livermore National Security, LLC.
# All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

is_freebsd && ! python3 -c 'import sysctl' 2>/dev/null && log_unsupported "python3 sysctl module missing"

set -A args  "" "-s \",\"" "-x" "-v" \
    "-f time,hit%,dh%,ph%,mh%"

log_assert "arcstat generates output and doesn't return an error code"

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
        log_must eval "zarcstat ${args[i]} > /dev/null"
        ((i = i + 1))
done
log_pass "arcstat generates output and doesn't return an error code"
