#! /bin/ksh -p
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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Valid reservation values should be positive integers only.
#
# STRATEGY:
# 1) Form an array of invalid reservation values (negative and
# incorrectly formed)
# 2) Attempt to set each invalid reservation value in turn on a
# filesystem and volume.
# 3) Verify that attempt fails and the reservation value remains
# unchanged
#

verify_runnable "both"

log_assert "Verify invalid reservation values are rejected"

set -A suffix "b" "k" "m" "t" "p" "e" "K" "M" "G" "T" "P" "E" "kb" "Mb" "Gb" \
	"Tb" "Pb" "Eb" "KB" "MB" "GB" "TB" "PB" "EB"

set -A values '' '-1' '-1.0' '-1.8' '-9999999999999999' '0x1' '0b' '1b' '1.1b'

#
# Function to loop through a series of bad reservation
# values, checking they are when we attempt to set them
# on a dataset.
#
function set_n_check # data-set
{
	typeset obj=$1
	typeset -i i=0
	typeset -i j=0

	orig_resv_val=$(get_prop reservation $obj)

	while (( $i < ${#values[*]} )); do
		j=0
		while (( $j < ${#suffix[*]} )); do

			log_mustnot zfs set reservation=${values[$i]}${suffix[$j]} $obj

			new_resv_val=$(get_prop reservation $obj)

			if [[ $new_resv_val != $orig_resv_val ]]; then
				log_fail "$obj : reservation values changed " \
					"($orig_resv_val : $new_resv_val)"
			fi
			(( j = j + 1 ))
		done

	(( i = i + 1 ))
	done
}

for dataset in $TESTPOOL/$TESTFS $TESTPOOL/$TESTCTR $TESTPOOL/$TESTVOL
do
	set_n_check $dataset
done

log_pass "Invalid reservation values correctly rejected"
