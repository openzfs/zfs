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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_create/zfs_create.cfg
. $STF_SUITE/tests/functional/cli_root/zfs_create/properties.kshlib

#
# DESCRIPTION:
# 'zfs create [-b <blocksize> ] -V <size> <volume>' fails with badly formed
# <size> or <volume> arguments,including:
#	*Invalid volume size and volume name
#	*Invalid blocksize
#	*Incomplete component in the dataset tree
#	*The volume already exists
#	*The volume name beyond the maximal name length - 256.
#       *Same property set multiple times via '-o property=value'
#       *Filesystems's property set on volume
#
# STRATEGY:
# 1. Create an array of badly formed arguments
# 2. For each argument, execute 'zfs create -V <size> <volume>'
# 3. Verify an error is returned.
#

verify_runnable "global"

function cleanup
{
	typeset -i i
	typeset found

	#
	# check to see if there is any new fs created during the test
	# if so destroy it.
	#
	for dset in $(zfs list -H | awk '$1 ~ /\// {print $1}'); do
		found=false
		i=0
		while (( $i < ${#existed_fs[*]} )); do
			if [[ $dset == ${existed_fs[i]} ]]; then
				found=true
				break
			fi
			(( i = i  + 1 ))
		done

		#
		# new fs created during the test, cleanup it
		#
		if [[ $found == "false" ]]; then
			log_must zfs destroy -f $dset
		fi
	done
}

log_onexit cleanup

log_assert "Verify 'zfs create [-s] [-b <blocksize> ] -V <size> <volume>' fails with" \
    "badly-formed <size> or <volume> arguments."

set -A args "$VOLSIZE" "$TESTVOL1" \
	"$VOLSIZE $TESTVOL1" "0 $TESTPOOL/$TESTVOL1" \
	"-1gb $TESTPOOL/$TESTVOL1" "1g? $TESTPOOL/$TESTVOL1" \
	"1.01BB $TESTPOOL/$TESTVOL1" "1%g $TESTPOOL/$TESTVOL1" \
	"1g% $TESTPOOL/$TESTVOL1" "1g$ $TESTPOOL/$TESTVOL1" \
	"$m $TESTPOOL/$TESTVOL1" "1m$ $TESTPOOL/$TESTVOL1" \
	"1m! $TESTPOOL/$TESTVOL1" \
	"1gbb $TESTPOOL/blah" "1blah $TESTPOOL/blah" "blah $TESTPOOL/blah" \
	"$VOLSIZE $TESTPOOL" "$VOLSIZE $TESTPOOL/" "$VOLSIZE $TESTPOOL//blah"\
	"$VOLSIZE $TESTPOOL/blah@blah" "$VOLSIZE $TESTPOOL/blah^blah" \
	"$VOLSIZE $TESTPOOL/blah*blah" "$VOLSIZE $TESTPOOL/blah%blah" \
	"$VOLSIZE blah" "$VOLSIZE $TESTPOOL/$BYND_MAX_NAME" \
	"1m -b $TESTPOOL/$TESTVOL1" "1m -b 11k $TESTPOOL/$TESTVOL1" \
	"1m -b 511 $TESTPOOL/$TESTVOL1"

set -A options "" "-s"

datasetexists $TESTPOOL/$TESTVOL || \
		log_must zfs create -V $VOLSIZE $TESTPOOL/$TESTVOL

set -A existed_fs $(zfs list -H | awk '$1 ~ /\// {print $1}')

log_mustnot zfs create -V $VOLSIZE $TESTPOOL/$TESTVOL
log_mustnot zfs create -s -V $VOLSIZE $TESTPOOL/$TESTVOL

typeset -i i=0
typeset -i j=0
while (( i < ${#options[*]} )); do

	j=0
	while (( j < ${#args[*]} )); do
		log_mustnot zfs create ${options[$i]} -V ${args[$j]}
		log_mustnot zfs create -p ${options[$i]} -V ${args[$j]}

		((j = j + 1))
	done

	j=0
	while (( $j < ${#RW_VOL_PROP[*]} )); do
		log_mustnot zfs create ${options[$i]} -o ${RW_VOL_PROP[j]} \
		    -o ${RW_VOL_PROP[j]} -V $VOLSIZE $TESTPOOL/$TESTVOL1
		log_mustnot zfs create -p ${options[$i]} -o ${RW_VOL_PROP[j]} \
		    -o ${RW_VOL_PROP[j]} -V $VOLSIZE $TESTPOOL/$TESTVOL1
		((j = j + 1))
	done

	j=0
	while (( $j < ${#FS_ONLY_PROP[*]} )); do
		log_mustnot zfs create ${options[$i]} -o ${FS_ONLY_PROP[j]} \
		    -V $VOLSIZE $TESTPOOL/$TESTVOL1
		log_mustnot zfs create -p ${options[$i]} -o ${FS_ONLY_PROP[j]} \
		    -V $VOLSIZE $TESTPOOL/$TESTVOL1
		((j = j + 1))
	done

	((i = i + 1))
done

log_pass "'zfs create [-s][-b <blocksize>] -V <size> <volume>' fails as expected."
