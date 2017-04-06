#!/bin/ksh -p
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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify 'zfs get all' can get all properties for all datasets in the system
#
# STRATEGY:
#	1. Create datasets for testing
#	2. Issue 'zfs get all' command
#	3. Verify the command gets all available properties of all datasets
#

verify_runnable "both"

function cleanup
{
	[[ -e $propfile ]] && rm -f $propfile

	datasetexists $clone  && \
		log_must zfs destroy $clone
	for snap in $fssnap $volsnap ; do
		snapexists $snap && \
			log_must zfs destroy $snap
	done

	if [[ -n $globalzone ]] ; then
		for pool in $TESTPOOL1 $TESTPOOL2 $TESTPOOL3; do
			poolexists $pool && \
				log_must zpool destroy -f $pool
		done
		for file in `ls $TESTDIR1/poolfile*`; do
			rm -f $file
		done
	else
		for fs in $TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS2 $TESTPOOL/$TESTFS3; do
			datasetexists $fs && \
				log_must zfs destroy -rf $fs
		done
	fi
}

log_assert "Verify the functions of 'zfs get all' work."
log_onexit cleanup

typeset globalzone=""

if is_global_zone ; then
	globalzone="true"
fi

set -A opts "" "-r" "-H" "-p" "-rHp" "-o name" \
	"-s local,default,temporary,inherited,none" \
	"-o name -s local,default,temporary,inherited,none" \
	"-rHp -o name -s local,default,temporary,inherited,none"
set -A usrprops "a:b=c" "d_1:1_e=0f" "123:456=789"

fs=$TESTPOOL/$TESTFS
fssnap=$fs@$TESTSNAP
clone=$TESTPOOL/$TESTCLONE
volsnap=$TESTPOOL/$TESTVOL@$TESTSNAP

#set user defined properties for $TESTPOOL
for usrprop in ${usrprops[@]}; do
	log_must zfs set $usrprop $TESTPOOL
done
# create snapshot and clone in $TESTPOOL
log_must zfs snapshot $fssnap
log_must zfs clone $fssnap $clone
log_must zfs snapshot $volsnap

# collect datasets which can be set user defined properties
usrpropds="$clone $fs"

# collect all datasets which we are creating
allds="$fs $clone $fssnap $volsnap"

#create pool and datasets to guarantee testing under multiple pools and datasets.
file=$TESTDIR1/poolfile
typeset FILESIZE=$MINVDEVSIZE
(( DFILESIZE = $FILESIZE * 2 ))
typeset -i VOLSIZE=10485760
availspace=$(get_prop available $TESTPOOL)
typeset -i i=0

# make sure 'availspace' is larger then twice of FILESIZE to create a new pool.
# If any, we only totally create 3 pools for multple datasets testing to limit
# testing time
while (( availspace > DFILESIZE )) && (( i < 3 )) ; do
	(( i += 1 ))

	if [[ -n $globalzone ]] ; then
		log_must mkfile $FILESIZE ${file}$i
		eval pool=\$TESTPOOL$i
		log_must zpool create $pool ${file}$i
	else
		eval pool=$TESTPOOL/\$TESTFS$i
		log_must zfs create $pool
	fi

	#set user defined properties for testing
	for usrprop in ${usrprops[@]}; do
		log_must zfs set $usrprop $pool
	done

	#create datasets in pool
	log_must zfs create $pool/$TESTFS
	log_must zfs snapshot $pool/$TESTFS@$TESTSNAP
	log_must zfs clone $pool/$TESTFS@$TESTSNAP $pool/$TESTCLONE

	if [[ -n $globalzone ]] ; then
		log_must zfs create -V $VOLSIZE $pool/$TESTVOL
	else
		log_must zfs create $pool/$TESTVOL
	fi

	ds=`zfs list -H -r -o name -t filesystem,volume $pool`
	usrpropds="$usrpropds $pool/$TESTFS $pool/$TESTCLONE $pool/$TESTVOL"
	allds="$allds $pool/$TESTFS $pool/$TESTCLONE $pool/$TESTVOL \
		$pool/$TESTFS@$TESTSNAP"

	availspace=$(get_prop available $TESTPOOL)
done

#the expected number of property for each type of dataset in this testing
typeset -i fspropnum=27
typeset -i snappropnum=8
typeset -i volpropnum=15
propfile=/var/tmp/allpropfile.$$

typeset -i i=0
typeset -i propnum=0
typeset -i failflag=0
while (( i < ${#opts[*]} )); do
	[[ -e $propfile ]] && rm -f $propfile
	log_must eval "zfs get ${opts[i]} all >$propfile"

	for ds in $allds; do
		grep $ds $propfile >/dev/null 2>&1
		(( $? != 0 )) && \
			log_fail "There is no property for" \
				"dataset $ds in 'get all' output."

		propnum=`cat $propfile | awk '{print $1}' | \
			grep "${ds}$" | wc -l`
		ds_type=`zfs get -H -o value type $ds`
		case $ds_type in
			filesystem )
				(( propnum < fspropnum )) && \
				(( failflag += 1 ))
				;;
			snapshot )
				(( propnum < snappropnum )) && \
				(( failflag += 1 ))
				;;
			volume )
				(( propnum < volpropnum )) && \
				(( failflag += 1 ))
				;;
		esac

		(( failflag != 0 )) && \
			log_fail " 'zfs get all' fails to get out " \
				"all properties for dataset $ds."

		(( propnum = 0 ))
		(( failflag = 0 ))
	done

	(( i += 1 ))
done

log_note "'zfs get' can get particular property for all datasets with that property."

function do_particular_prop_test #<property> <suitable datasets>
{
	typeset	props="$1"
	typeset ds="$2"

	for prop in ${commprops[*]}; do
		ds=`zfs get -H -o name $prop`

		[[ "$ds" != "$allds" ]] && \
			log_fail "The result datasets are $ds, but all suitable" \
				"datasets are $allds for the property $prop"
	done
}

# Here, we do a testing for user defined properties and the most common properties
# for all datasets.
commprop="type creation used referenced compressratio"
usrprop="a:b d_1:1_e 123:456"

do_particular_prop_test "$commprop" "$allds"
do_particular_prop_test "$usrprop" "$usrpropds"

log_pass "'zfs get all' works as expected."
