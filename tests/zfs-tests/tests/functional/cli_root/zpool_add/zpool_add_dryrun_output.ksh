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

typeset STR_DRYRUN="would update '$TESTPOOL' to the following configuration:"
typeset VDEV_PREFIX="$TEST_BASE_DIR/filedev"

#
# DESCRIPTION:
# 'zpool add -n <pool> <vdev> ...' can display the correct configuration
#
# STRATEGY:
# 1. Create different storage pools, use -n to add devices to the pool and
#    verify the output is as expected.
# 2. Create a pool with a hole vdev and verify it's not listed with add -n.
#

typeset -a dev=(
	"${VDEV_PREFIX}00" "${VDEV_PREFIX}01" "${VDEV_PREFIX}02"
	"${VDEV_PREFIX}03" "${VDEV_PREFIX}04" "${VDEV_PREFIX}05"
	"${VDEV_PREFIX}06" "${VDEV_PREFIX}07" "${VDEV_PREFIX}08"
	"${VDEV_PREFIX}09" "${VDEV_PREFIX}10" "${VDEV_PREFIX}11"
)

typeset -a tests=(
    (
	tree="'${dev[0]}' log '${dev[1]}' special '${dev[2]}' dedup '${dev[3]}'"
	add="spare '${dev[4]}' cache '${dev[5]}'"
	want="$STR_DRYRUN

	$TESTPOOL
	  ${dev[0]}
	dedup
	  ${dev[3]}
	special
	  ${dev[2]}
	logs
	  ${dev[1]}
	cache
	  ${dev[5]}
	spares
	  ${dev[4]}"
    )
    (
	tree="'${dev[0]}' log '${dev[1]}' special '${dev[2]}' dedup '${dev[3]}' \
	    spare '${dev[4]}' cache '${dev[5]}'"

	add="'${dev[6]}' log '${dev[7]}' special '${dev[8]}' dedup '${dev[9]}' \
	    spare '${dev[10]}' cache '${dev[11]}'"

	want="$STR_DRYRUN

	$TESTPOOL
	  ${dev[0]}
	  ${dev[6]}
	dedup
	  ${dev[3]}
	  ${dev[9]}
	special
	  ${dev[2]}
	  ${dev[8]}
	logs
	  ${dev[1]}
	  ${dev[7]}
	cache
	  ${dev[5]}
	  ${dev[11]}
	spares
	  ${dev[4]}
	  ${dev[10]}"
    )
    (
	tree="mirror '${dev[0]}' '${dev[1]}' \
	    log mirror '${dev[2]}' '${dev[3]}' \
	    dedup mirror '${dev[6]}' '${dev[7]}' \
	    spare '${dev[8]}'"

	add="special mirror '${dev[4]}' '${dev[5]}' \
	    spare '${dev[9]}' cache '${dev[10]}' '${dev[11]}'"

	want="$STR_DRYRUN

	$TESTPOOL
	  mirror-0
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
	destroy_pool "$TESTPOOL"
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
	add="${tests[$i].add}"
	want="${tests[$i].want}"

	log_must eval zpool create "$TESTPOOL" $tree
	log_must poolexists "$TESTPOOL"
	typeset out
	out="$(eval zpool add -n '$TESTPOOL' $add)"
	if [[ $? -ne 0 ]]; then
		log_fail eval "zpool add -n '$TESTPOOL' $add"
	fi
	if [[ "$out" != "$want" ]]; then
		log_note "Got:"
		log_note "$out"
		log_note "but expected:"
		log_note "$want"
		log_fail "Dry run does not display config correctly"
	fi
	log_must destroy_pool "$TESTPOOL"
done

# Make sure hole vdevs are skipped in output.
log_must eval "zpool create '$TESTPOOL' '${dev[0]}' log '${dev[1]}' \
    cache '${dev[2]}'"

# Create a hole vdev.
log_must eval "zpool remove '$TESTPOOL' '${dev[1]}'"
typeset out
out="$(eval zpool add -n '$TESTPOOL' '${dev[1]}')"
if [[ $? -ne 0 ]]; then
    log_fail eval "zpool add -n '$TESTPOOL' '${dev[1]}'"
fi
log_mustnot grep -qE '[[:space:]]+hole' <<<"$out"

log_pass "'zpool add -n <pool> <vdev> ...' displays config correctly."
