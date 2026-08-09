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
# Copyright 2020 Attila Fülöp <attila@fueloep.org>
#

. $STF_SUITE/include/libtest.shlib

typeset STR_DRYRUN="would create '$TESTPOOL' with the following layout:"
typeset VDEV_PREFIX="$TEST_BASE_DIR/filedev"

#
# DESCRIPTION:
# 'zpool create -n <pool> <vdev> ...' can display the correct configuration
#
# STRATEGY:
# 1. Create -n a storage pool and verify the output is as expected.
#

typeset -a dev=(
	"${VDEV_PREFIX}00" "${VDEV_PREFIX}01" "${VDEV_PREFIX}02"
	"${VDEV_PREFIX}03" "${VDEV_PREFIX}04" "${VDEV_PREFIX}05"
	"${VDEV_PREFIX}06" "${VDEV_PREFIX}07" "${VDEV_PREFIX}08"
	"${VDEV_PREFIX}09" "${VDEV_PREFIX}10" "${VDEV_PREFIX}11"
)

typeset -a tests=(
    (
	tree="'${dev[0]}' '${dev[1]}' log '${dev[2]}' '${dev[3]}' \
	    special '${dev[4]}' '${dev[5]}' dedup '${dev[6]}' '${dev[7]}' \
		spare '${dev[8]}' '${dev[9]}' cache '${dev[10]}' '${dev[11]}'"

	want="$STR_DRYRUN

	$TESTPOOL
	  ${dev[0]}
	  ${dev[1]}
	dedup
	  ${dev[6]}
	  ${dev[7]}
	special
	  ${dev[4]}
	  ${dev[5]}
	logs
	  ${dev[2]}
	  ${dev[3]}
	cache
	  ${dev[10]}
	  ${dev[11]}
	spares
	  ${dev[8]}
	  ${dev[9]}"
    )
    (
	tree="mirror '${dev[0]}' '${dev[1]}' \
	    log mirror '${dev[2]}' '${dev[3]}' \
	    special mirror '${dev[4]}' '${dev[5]}' \
	    dedup mirror '${dev[6]}' '${dev[7]}' \
		spare '${dev[8]}' '${dev[9]}' \
	    cache '${dev[10]}' '${dev[11]}'"

	want="$STR_DRYRUN

	$TESTPOOL
	  mirror
	    ${dev[0]}
	    ${dev[1]}
	dedup
	  mirror
	    ${dev[6]}
	    ${dev[7]}
	special
	  mirror
	    ${dev[4]}
	    ${dev[5]}
	logs
	  mirror
	    ${dev[2]}
	    ${dev[3]}
	cache
	  ${dev[10]}
	  ${dev[11]}
	spares
	  ${dev[8]}
	  ${dev[9]}"
    )
)

verify_runnable "global"

function cleanup
{
	rm -f "$VDEV_PREFIX"*
}

log_assert "'zpool add -n <pool> <vdev> ...' can display the configuration"

log_onexit cleanup

# Create needed file vdevs.
for (( i=0; i < ${#dev[@]}; i+=1 )); do
	log_must truncate -s $SPA_MINDEVSIZE "${dev[$i]}"
done

# Foreach test create pool, add -n devices and check output.
for (( i=0; i < ${#tests[@]}; i+=1 )); do
	tree="${tests[$i].tree}"
	want="${tests[$i].want}"
	typeset out
	out="$(eval zpool create -n '$TESTPOOL' $tree)"
	if [[ $? -ne 0 ]]; then
		log_fail eval "zpool create -n '$TESTPOOL' $tree"
	fi
	if [[ "$out" != "$want" ]]; then
		log_note "Got:"
		log_note "$out"
		log_note "but expected:"
		log_note "$want"
		log_fail "Dry run does not display config correctly"
	fi
done

log_pass "'zpool add -n <pool> <vdev> ...' displays config correctly."
