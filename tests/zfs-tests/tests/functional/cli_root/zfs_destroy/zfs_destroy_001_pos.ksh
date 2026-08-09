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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy.cfg
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy_common.kshlib

#
# DESCRIPTION:
#	'zfs destroy -r|-rf|-R|-Rf <fs|ctr|vol|snap>' should recursively destroy
#	all children and clones based on options.
#
# STRATEGY:
#	1. Create test environment according to options. There are three test
#	models can be created. Only ctr, fs & vol; with snap; with clone.
#	2. According to option, make the dataset busy or not.
#	3. Run 'zfs destroy [-rRf] <dataset>'
#	4. According to dataset and option, check if get the expected results.
#

verify_runnable "both"

#
# According to parameters, 1st, create suitable testing environment. 2nd,
# run 'zfs destroy $opt <dataset>'. 3rd, check the system status.
#
# $1 option of 'zfs destroy'
# $2 dataset will be destroyed.
#
function test_n_check
{
	typeset opt=$1
	typeset dtst=$2

	if ! is_global_zone ; then
		if [[ $dtst == $VOL || $dtst == $VOLSNAP ]]; then
			log_note "UNSUPPORTED: Volume are unavailable in LZ."
			return
		fi
	fi

	# '-f' has no effect on non-filesystems
	if [[ $opt == -f ]]; then
		if [[ $dtst != $FS ]]; then
			log_note "UNSUPPORTED: '-f ' is only available for " \
			    "leaf FS."
			return
		fi
	fi

	# Clean the test environment and make it clear.
	datasetexists $CTR && destroy_dataset $CTR -Rf

	# According to option create test compatible environment.
	case $opt in
		-r|-rf) setup_testenv snap ;;
		-R|-Rf) setup_testenv clone ;;
		-f)	setup_testenv ;;
		*)	log_fail "Incorrect option: '$opt'." ;;
	esac

	#
	# According to different dataset type, create busy condition when try to
	# destroy this dataset.
	#
	typeset mpt_dir
	case $dtst in
		$CTR|$FS)
			if [[ $opt == *f* ]]; then
				mpt_dir=$(get_prop mountpoint $FS)
				pidlist="$pidlist $(mkbusy \
				    $mpt_dir/$TESTFILE0)"
				log_note "mkbusy $mpt_dir/$TESTFILE0 " \
				    "(pidlist: $pidlist)"
				[[ -z $pidlist ]] && \
				    log_fail "Failure from mkbusy"
				log_mustnot zfs destroy -rR $dtst
			fi
			;;
		$VOL)
			if [[ $opt == *f* ]]; then
				pidlist="$pidlist $(mkbusy \
				    $TESTDIR1/$TESTFILE0)"
				log_note "mkbusy $TESTDIR1/$TESTFILE0 " \
				    "(pidlist: $pidlist)"
				[[ -z $pidlist ]] && \
				    log_fail "Failure from mkbusy"
				log_mustnot zfs destroy -rR $dtst
			fi
			;;
		$VOLSNAP)
			if [[ $opt == *f* ]]; then
				pidlist="$pidlist $(mkbusy \
				    $TESTDIR1/$TESTFILE0)"
				log_note "mkbusy $TESTDIR1/$TESTFILE0 " \
				    "(pidlist: $pidlist)"
				[[ -z $pidlist ]] && \
				    log_fail "Failure from mkbusy"
				log_must_busy zfs destroy -rR $dtst
				log_must zfs snapshot $dtst
			fi
			;;
		$FSSNAP)
			if [[ $opt == *f* ]]; then
				mpt_dir=$(snapshot_mountpoint $dtst)
				pidlist="$pidlist $(mkbusy $mpt_dir)"
				log_note "mkbusy $mpt_dir (pidlist: $pidlist)"
				[[ -z $pidlist ]] && \
				    log_fail "Failure from mkbusy"
				if is_linux ; then
					log_mustnot zfs destroy -rR $dtst
				else
					log_must zfs destroy -rR $dtst
					log_must zfs snapshot $dtst
				fi
			fi
			;;
		*)	log_fail "Unsupported dataset: '$dtst'."
	esac

	# Kill any lingering instances of mkbusy, and clear the list.
	if is_linux ; then
		[[ -z $pidlist ]] || log_must kill -TERM $pidlist
		pidlist=""
		log_mustnot pgrep -fl mkbusy
	fi

	# Firstly, umount ufs filesystem which was created by zfs volume.
	if is_global_zone; then
		log_must umount -f $TESTDIR1
	fi

	# Invoke 'zfs destroy [-rRf] <dataset>'
	log_must_busy zfs destroy $opt $dtst
	block_device_wait

	# Kill any lingering instances of mkbusy, and clear the list.
	if ! is_linux ; then
		[[ -z $pidlist ]] || log_must kill -TERM $pidlist
		pidlist=""
		log_mustnot pgrep -fl mkbusy
	fi

	case $dtst in
		$CTR)	check_dataset datasetnonexists \
					$CTR $FS $VOL $FSSNAP $VOLSNAP
			if [[ $opt == *R* ]]; then
				check_dataset datasetnonexists \
					$FSCLONE $VOLCLONE
			fi
			;;
		$FS)	check_dataset datasetexists $CTR $VOL
			check_dataset datasetnonexists $FS
			if [[ $opt != -f ]]; then
				check_dataset datasetexists $VOLSNAP
				check_dataset datasetnonexists $FSSNAP
			fi
			if [[ $opt == *R* ]]; then
				check_dataset datasetexists $VOLCLONE
				check_dataset datasetnonexists $FSCLONE
			fi
			;;
		$VOL)	check_dataset datasetexists $CTR $FS $FSSNAP
			check_dataset datasetnonexists $VOL $VOLSNAP
			if [[ $opt == *R* ]]; then
				check_dataset datasetexists $FSCLONE
				check_dataset datasetnonexists $VOLCLONE
			fi
			;;
		$FSSNAP)
			check_dataset datasetexists $CTR $FS $VOL $VOLSNAP
			check_dataset datasetnonexists $FSSNAP
			if [[ $opt == *R* ]]; then
				check_dataset datasetexists $VOLCLONE
				check_dataset datasetnonexists $FSCLONE
			fi
			;;
		$VOLSNAP)
			check_dataset datasetexists $CTR $FS $VOL $FSSNAP
			check_dataset datasetnonexists $VOLSNAP
			if [[ $opt == *R* ]]; then
				check_dataset datasetexists $FSCLONE
				check_dataset datasetnonexists $VOLCLONE
			fi
			;;
	esac

	log_note "'zfs destroy $opt $dtst' passed."
}

log_assert "'zfs destroy -r|-R|-f|-rf|-Rf <fs|ctr|vol|snap>' should " \
	"recursively destroy all children."
log_onexit cleanup_testenv

typeset dtst=""
typeset opt=""
typeset pidlist=""
for dtst in $CTR $FS $VOL $FSSNAP $VOLSNAP; do
	for opt in "-r" "-R" "-f" "-rf" "-Rf"; do
		log_note "Starting test: zfs destroy $opt $dtst"
		test_n_check $opt $dtst
	done
done

log_pass "'zfs destroy -r|-R|-f|-rf|-Rf <fs|ctr|vol|snap>' passed."
