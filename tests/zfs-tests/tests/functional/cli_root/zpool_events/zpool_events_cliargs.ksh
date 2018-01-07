#!/bin/ksh -p
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zpool events' should only work with supported options.
#
# STRATEGY:
# 1. Verify every supported option is accepted
# 2. Verify supported options raise an error with unsupported arguments
# 3. Verify other unsupported options raise an error
#

verify_runnable "both"

function log_must_follow # <command>
{
	typeset command="$1"

	log_must eval "$command > /dev/null &"
	pid=$!
	sleep 3
	kill $pid
	if [[ $? -ne 0 ]]; then
		log_fail "'$command' does not work as expected."
	else
		log_note "'$command' works successfully."
	fi
}

log_assert "'zpool events' should only work with supported options."

typeset goodopts=("" "-v" "-H" "-f" "-vH" "-vf" "-Hf" "-vHf")
typeset badopts=("-vV" "-Hh" "-fF" "-cC" "-x" "-o" "-")

# 1. Verify every supported option is accepted
for opt in ${goodopts[@]}
do
	# when in 'follow' mode we can't use log_must()
	if [[ $opt =~ 'f' ]]; then
		log_must_follow "zpool events $opt"
		log_must_follow "zpool events $opt $TESTPOOL"
	else
		log_must eval "zpool events $opt > /dev/null"
		log_must eval "zpool events $opt $TESTPOOL > /dev/null"
	fi
done

# 2.1 Verify supported options raise an error with unsupported arguments
for opt in ${goodopts[@]}
do
	log_mustnot zpool events $opt "/tmp/"
	log_mustnot zpool events $opt "$TESTPOOL/fs"
	log_mustnot zpool events $opt "$TESTPOOL@snap"
	log_mustnot zpool events $opt "$TESTPOOL#bm"
	log_mustnot zpool events $opt "$TESTPOOL" "$TESTPOOL"
done

# 2.2 Additionally, 'zpool events -c' does not support any other option|argument
log_must eval "zpool events -c > /dev/null"
log_mustnot zpool events -c "$TESTPOOL"
for opt in ${goodopts[@]}
do
	log_mustnot zpool events -c $opt
done

# 3. Verify other unsupported options raise an error
for opt in ${badopts[@]}
do
	log_mustnot zpool events $opt
	log_mustnot zpool events $opt "$TESTPOOL"
done

log_pass "'zpool events' only works with supported options."
