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
# Copyright 2019 Joyent, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/zfs_create_common.kshlib

#
# DESCRIPTION:
# zfs create -n should perform basic sanity checking but should never create a
# dataset.  If -v and/or -P are used, it should verbose about what would be
# created if sanity checks pass.
#
# STRATEGY:
# 1. Attempt to create a file system and a volume using various combinations of
#    -n with -v and -P.
#

verify_runnable "both"

#
# Verifies that valid commands with -n and without -[vP]:
# - succeed
# - do not create a dataset
# - do not generate output
#
function dry_create_no_output
{
	typeset -a cmd=(zfs create -n "$@")

	log_note "$0: ${cmd[@]}"
	log_must "${cmd[@]}"
	datasetexists "$TESTPOOL/$TESTFS1" &&
	    log_fail "$TESTPOOL/$TESTFS1 unexpectedly created by '${cmd[@]}'"
	typeset out=$("${cmd[@]}" 2>&1)
	[[ -z "$out" ]] ||
	    log_fail "unexpected output '$out' from '${cmd[@]}'"
}

#
# Verifies that commands with invalid properties or invalid property values
# - fail
# - do not create a dataset
# - generate a message on stderr
#
function dry_create_error
{
	typeset -a cmd=(zfs create -n "$@")

	log_note "$0: ${cmd[@]}"
	log_mustnot "${cmd[@]}"
	datasetexists "$TESTPOOL/$TESTFS1" &&
	    log_fail "$TESTPOOL/$TESTFS1 unexpectedly created by '${cmd[@]}'"
	typeset out=$("${cmd[@]}" 2>&1 >/dev/null)
	[[ -z "$out" ]] &&
	    log_fail "expected an error message but got none from '${cmd[@]}'"
}

#
# Verifies that dry-run commands with parseable output
# - succeed
# - do not create datasets
# - generate parseable output on stdout
# - output matches expectations
#
function dry_create_parseable
{
	typeset -n exp=$1
	shift
	typeset -a cmd=(zfs create -Pn "$@")
	typeset ds=${cmd[${#cmd[@]} - 1]}
	typeset out
	typeset -a toks
	typeset -a props
	typeset found_create=false

	log_note "$0: ${cmd[@]}"
	out=$("${cmd[@]}")
	(( $? == 0 )) ||
	    log_fail "unexpected failure getting stdout from '${cmd[@]}'"
	datasetexists "$TESTPOOL/$TESTFS1" &&
	    log_fail "$TESTPOOL/$TESTFS1 unexpectedly created by '${cmd[@]}'"
	echo "$out" | while IFS=$'\t' read -A toks; do
		log_note "verifying ${toks[@]}"
		case ${toks[0]} in
		create)
			log_must test "${#toks[@]}" -eq 2
			log_must test "${toks[1]}" == "$ds"
			found_create="yes, I found create"
			;;
		property)
			log_must test "${#toks[@]}" -eq 3
			typeset prop=${toks[1]}
			typeset val=${toks[2]}
			if [[ -z "${exp[$prop]}" ]]; then
				log_fail "unexpectedly got property '$prop'"
			fi
			# We may not know the exact value a property will take
			# on.  This is the case for at least refreservation.
			if [[ ${exp[$prop]} != "*" ]]; then
				log_must test "${exp[$prop]}" == "$val"
			fi
			unset exp[$prop]
			;;
		*)
			log_fail "Unexpected line ${toks[@]}"
			;;
		esac
	done

	log_must test "$found_create" == "yes, I found create"
	log_must test "extra props: ${!exp[@]}" == "extra props: "
}

function cleanup
{
	if datasetexists "$TESTPOOL/$TESTFS1"; then
		log_must zfs destroy -r "$TESTPOOL/$TESTFS1"
	fi
}
log_onexit cleanup

log_assert "zfs create -n creates nothing but can describe what would be" \
	"created"

# Typical creations should succeed
dry_create_no_output "$TESTPOOL/$TESTFS1"
dry_create_no_output -V 10m "$TESTPOOL/$TESTFS1"
# It shouldn't do a space check right now
dry_create_no_output -V 100t "$TESTPOOL/$TESTFS1"
# It shouldn't create parent datasets either
dry_create_no_output -p "$TESTPOOL/$TESTFS1/$TESTFS2"
dry_create_no_output -pV 10m "$TESTPOOL/$TESTFS1/$TESTFS2"

# Various invalid properties should be recognized and result in an error
dry_create_error -o nosuchprop=42 "$TESTPOOL/$TESTFS1"
dry_create_error -b 1234 -V 10m  "$TESTPOOL/$TESTFS1"

# Parseable output should be parseable.
typeset -A expect
expect=([compression]=on)
dry_create_parseable expect -o compression=on "$TESTPOOL/$TESTFS1"

# Sparse volumes should not get a gratuitous refreservation
expect=([volblocksize]=4096 [volsize]=$((1024 * 1024 * 10)))
dry_create_parseable expect -b 4k -V 10m -s "$TESTPOOL/$TESTFS1"

# Non-sparse volumes should have refreservation
expect=(
    [volblocksize]=4096
    [volsize]=$((1024 * 1024 * 10))
    [refreservation]="*"
)
dry_create_parseable expect -b 4k -V 10m "$TESTPOOL/$TESTFS1"

log_pass "zfs create -n creates nothing but can describe what would be" \
	"created"
