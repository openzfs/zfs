#! /bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright (c) 2015 by Lawrence Livermore National Security, LLC.
# All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

# Keep the following test until Python 3 is installed on all test systems,
# then remove
python3 -V 2>&1 > /dev/null
if (( $? )); then
        log_unsupported "Python3 is not installed"
fi


# Some systems have Python 3 installed, but only older versions that don't
# have the subprocess.run() functionality. We catch these with a separate
# test. Remove this when all systems have reached 3.5 or greater
VERSIONPYTEST=$(python3 -V)
if [[ ${VERSIONPYTEST:9:1} -lt 5 ]]; then
        log_unsupported "Python3 must be version 3.5 or greater"
fi


set -A args  "" "-a" "-d" "-p 1" "-g" "-s arc" "-r"
log_assert "arc_summary3.py generates output and doesn't return an error code"

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
        log_must eval "arc_summary3.py ${args[i]} > /dev/null"
        ((i = i + 1))
done

log_pass "arc_summary3.py generates output and doesn't return an error code"
