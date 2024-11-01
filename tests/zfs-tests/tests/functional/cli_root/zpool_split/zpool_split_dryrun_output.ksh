#!/bin/ksh -p
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
# Copyright 2020 Attila Fülöp <attila@fueloep.org>
#

. $STF_SUITE/include/libtest.shlib

typeset NEWPOOL="${TESTPOOL}split"
typeset STR_DRYRUN="would create '$NEWPOOL' with the following layout:"
typeset VDEV_PREFIX="$TEST_BASE_DIR/filedev"

#
# DESCRIPTION:
# 'zpool split -n <pool> <newpool> [<vdev> ...]' can display the correct
# configuration
#
# STRATEGY:
# 1. Create a mirrored storage pool, split -n and verify the output is as
#    expected.
#

typeset -a dev=(
	"${VDEV_PREFIX}00" "${VDEV_PREFIX}01" "${VDEV_PREFIX}02"
	"${VDEV_PREFIX}03" "${VDEV_PREFIX}04" "${VDEV_PREFIX}05"
	"${VDEV_PREFIX}06" "${VDEV_PREFIX}07" "${VDEV_PREFIX}08"
	"${VDEV_PREFIX}09" "${VDEV_PREFIX}10" "${VDEV_PREFIX}11"
)

typeset -a tests=(
    # Test for hole.
    (
	tree="mirror '${dev[0]}' '${dev[1]}' log mirror '${dev[2]}' '${dev[3]}' \
	    special mirror '${dev[4]}' '${dev[5]}'"

	devs=""
	want="$STR_DRYRUN

	$NEWPOOL
	  ${dev[1]}
	special
	  ${dev[5]}"
    )
    (
	tree="mirror '${dev[0]}' '${dev[1]}' log mirror '${dev[2]}' '${dev[3]}' \
	    special mirror '${dev[4]}' '${dev[5]}'"

	devs="'${dev[0]}' '${dev[4]}'"
	want="$STR_DRYRUN

	$NEWPOOL
	  ${dev[0]}
	special
	  ${dev[4]}"
    )

	# Full set of vdev types.
    (
	tree="mirror '${dev[0]}' '${dev[1]}'
	    dedup mirror '${dev[2]}' '${dev[3]}' \
	    special mirror '${dev[4]}' '${dev[5]}' \
	    cache '${dev[6]}' '${dev[7]}' \
	    spare '${dev[8]}' '${dev[9]}'\
		log mirror '${dev[10]}' '${dev[11]}'"

	devs=""
	want="$STR_DRYRUN

	$NEWPOOL
	  ${dev[1]}
	dedup
	  ${dev[3]}
	special
	  ${dev[5]}"
    )
    (
	tree="mirror '${dev[0]}' '${dev[1]}'
	    dedup mirror '${dev[2]}' '${dev[3]}' \
	    special mirror '${dev[4]}' '${dev[5]}' \
	    cache '${dev[6]}' '${dev[7]}' \
	    spare '${dev[8]}' '${dev[9]}'\
		log mirror '${dev[10]}' '${dev[11]}'"

	devs="'${dev[0]}' '${dev[2]}' '${dev[4]}'"
	want="$STR_DRYRUN

	$NEWPOOL
	  ${dev[0]}
	dedup
	  ${dev[2]}
	special
	  ${dev[4]}"
    )
)

verify_runnable "global"

function cleanup
{
	destroy_pool "$TESTPOOL"
	rm -f "$VDEV_PREFIX"*
}

log_assert \
"'zpool split -n <pool> <newpool> [<vdev>]...' can display the configuration"

log_onexit cleanup

# Create needed file vdevs.
for (( i=0; i < ${#dev[@]}; i+=1 )); do
	log_must truncate -s $SPA_MINDEVSIZE "${dev[$i]}"
done

# Foreach test create pool, add -n devices and check output.
for (( i=0; i < ${#tests[@]}; i+=1 )); do
	tree="${tests[$i].tree}"
	devs="${tests[$i].devs}"
	want="${tests[$i].want}"

	log_must eval zpool create "$TESTPOOL" $tree
	log_must poolexists "$TESTPOOL"
	typeset out="$(log_must eval "zpool split -n \
	    '$TESTPOOL' '$NEWPOOL' $devs" | sed /^SUCCESS/d)"

	if [[ "$out" != "$want" ]]; then
		log_fail "Got:\n" "$out" "\nbut expected:\n" "$want"
	fi
	log_must destroy_pool "$TESTPOOL"
done

log_pass \
"'zpool split -n <pool> <newpool> [<vdev>]...' displays config correctly."
