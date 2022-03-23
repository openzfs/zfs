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
# zfs create -P without -n should be verbose about dataset creation.
#
# STRATEGY:
# 1. Attempt to create a file system and a volume using various properties
#    and -P
# 2. Exercise the combination of -p and -P.
#

verify_runnable "both"

#
# Verifies that non dry-run commands with parseable output
# - succeed
# - create datasets
# - generate parseable output on stdout
# - output matches expectations
#
function dry_create_parseable
{
	typeset -n exp=$1
	shift
	typeset -a cmd=(zfs create -P "$@")
	typeset ds=${cmd[${#cmd[@]} - 1]}
	typeset out
	typeset -a toks
	typeset -a props
	typeset found_create=false
	typeset create_ancestors=
	typeset opt

	# Parse the arguments to see if -p was used.
	while getopts :PV:b:ospv opt; do
		case $opt in
		p)	create_ancestors=needed ;;
		*)	continue ;;
		esac
	done

	log_note "$0: ${cmd[@]}"
	out=$("${cmd[@]}") ||
	    log_fail "unexpected failure getting stdout from '${cmd[@]}'"
	datasetexists "$TESTPOOL/$TESTFS1" ||
	    log_fail "$TESTPOOL/$TESTFS1 unexpectedly created by '${cmd[@]}'"
	while IFS=$'\t' read -A toks; do
		log_note "verifying ${toks[@]}"
		case ${toks[0]} in
		create_ancestors)
			case "$create_ancestors" in
			needed)
				log_must test "${toks[1]}" == "$ds"
				create_ancestors="found ${toks[1]}"
				;;
			found*)
				log_fail "multiple ancestor creation" \
				    "$create_ancestors and ${toks[1]}"
				;;
			"")
				log_fail "unexpected create_ancestors"
				;;
			*)
				log_fail "impossible error: fix the test"
				;;
			esac
			;;
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
	done <<<"$out"

	log_must test "$found_create" == "yes, I found create"
	log_must test "extra props: ${!exp[@]}" == "extra props: "

	case "$create_ancestors" in
	"")
		log_must_busy zfs destroy "$ds"
		;;
	"found $ds")
		log_must_busy zfs destroy -r "$(echo "$ds" | cut -d/ -f1-2)"
		;;
	needed)
		log_fail "Expected but did not find create_ancestors"
		;;
	*)
		log_fail "Unexpected value for create_ancestors:" \
		    "$create_ancestors"
		;;
	esac
}

function cleanup
{
	datasetexists "$TESTPOOL/$TESTFS1" && \
		destroy_dataset "$TESTPOOL/$TESTFS1" -r
}
log_onexit cleanup

log_assert "zfs create -v creates datasets verbosely"

# Parseable output should be parseable.
typeset -A expect
expect=([compression]=on)
dry_create_parseable expect -o compression=on "$TESTPOOL/$TESTFS1"

# Ancestor creation with -p should emit relevant line
expect=([compression]=on)
dry_create_parseable expect -p -o compression=on "$TESTPOOL/$TESTFS1"
expect=([compression]=on)
dry_create_parseable expect -p -o compression=on "$TESTPOOL/$TESTFS1/$TESTVOL"

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

log_pass "zfs create -v creates datasets verbosely"
