#! /bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
# Copyright (c) 2022 Hewlett Packard Enterprise Development LP.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/inheritance/inherit.kshlib

#
# DESCRIPTION:
# Test that properties are correctly inherited using 'zfs set',
# 'zfs inherit' and 'zfs inherit -r'.
#
# STRATEGY:
# 1) Read a configX.cfg file and create the specified datasets
# 2) Read a stateX.cfg file and execute the commands within it
# and verify that the properties have the correct values
# 3) Repeat steps 1-2 for each configX and stateX files found.
#

verify_runnable "global"

log_assert "Test properties are inherited correctly"

#
# Simple function to create specified datasets.
#
function create_dataset { #name type disks
	typeset dataset=$1
	typeset type=$2
	typeset disks=$3

	if [[ $type == "POOL" ]]; then
		create_pool "$dataset" "$disks"
	elif [[ $type == "CTR" ]]; then
		log_must zfs create $dataset
		log_must zfs set canmount=off $dataset
	elif [[ $type == "FS" ]]; then
		log_must zfs create $dataset
	else
		log_fail "Unrecognised type $type"
	fi

	list="$list $dataset"
}

#
# Function to walk through all the properties in a
# dataset, setting them to a 'local' value if required.
#
function init_props { #dataset init_code
	typeset dataset=$1
	typeset init_code=$2
	typeset dir=$3

	typeset -i i=0

	#
	# Though the effect of '-' and 'default' is the same we
	# call them out via a log_note to aid in debugging the
	# config files
	#
	if [[ $init_code == "-" ]]; then
		log_note "Leaving properties for $dataset unchanged."
		[[ $def_recordsize == 0 ]] && \
		    update_recordsize $dataset $init_code
		return;
	elif [[ $init_code == "default" ]]; then
		log_note "Leaving properties for $dataset at default values."
		[[ $def_recordsize == 0 ]] && \
		    update_recordsize $dataset $init_code
		return;
	elif [[ $init_code == "local" ]]; then
		log_note "Setting properties for $dataset to local values."
		while (( i <  ${#prop[*]} )); do
			if [[ ${prop[i]} == "recordsize" ]]; then
				update_recordsize $dataset $init_code
			else
				if [[ ${prop[i]} == "mountpoint" ]]; then
					set_n_verify_prop ${prop[i]} \
					    ${local_val[((i/2))]}.$dir $dataset
				else
					set_n_verify_prop ${prop[i]} \
					    ${local_val[((i/2))]} $dataset
				fi
			fi

			((i = i + 2))
		done
	else
		log_fail "Unrecognised init code $init_code"
	fi
}

#
# We enter this function either to update the recordsize value
# in the default array, or to update the local value array.
#
function update_recordsize { #dataset init_code
	typeset dataset=$1
	typeset init_code=$2
	typeset idx=0
	typeset record_val

	#
	# First need to find where the recordsize property is
	# located in the arrays
	#
	while (( idx <  ${#prop[*]} )); do
		[[ ${prop[idx]} == "recordsize" ]] && break

		((idx = idx + 2))
	done

	((idx = idx / 2))
	record_val=`get_prop recordsize $dataset`
	if [[ $init_code == "-" || $init_code == "default" ]]; then
		def_val[idx]=$record_val
		def_recordsize=1
	elif [[ $init_code == "local" ]]; then
		log_must zfs set recordsize=$record_val $dataset
		local_val[idx]=$record_val
	fi
}

#
# The mountpoint property is slightly different from other properties and
# so is handled here. For all other properties if they are set to a specific
# value at a higher level in the data hierarchy (i.e. checksum=on) then that
# value propagates down the hierarchy unchanged, with the source field being
# set to 'inherited from <higher dataset>'.
#
# The mountpoint property is different in that while the value propagates
# down the hierarchy, the value at each level is determined by a combination
# of the top-level value and the current level in the hierarchy.
#
# For example consider the case where we have a pool (called pool1), containing
# a dataset (ctr) which in turn contains a filesystem (fs). If we set the
# mountpoint of the pool to '/mnt2' then the mountpoints for the dataset and
# filesystem are '/mnt2/ctr' and /mnt2/ctr/fs' respectively, with the 'source'
# field being set to 'inherited from pool1'.
#
# So at the filesystem level to calculate what our mountpoint property should
# be set to we walk back up the hierarchy sampling the mountpoint property at
# each level and forming up the expected mountpoint value piece by piece until
# we reach the level specified in the 'source' field, which in this example is
# the top-level pool.
#
function get_mntpt_val #dataset src index
{
	typeset dataset=$1
	typeset src=$2
	typeset idx=$3
	typeset new_path=""
	typeset dset
	typeset mntpt=""

	if [[ $src == "local" ]]; then
		# Extract mount points specific to datasets
		if [[ $dataset == "TESTPOOL" ]]; then
			mntpt=${local_val[idx]}.1
		elif [[ $dataset == "TESTPOOL/TESTCTR" ]]; then
			mntpt=${local_val[idx]}.2
		else
			mntpt=${local_val[idx]}.3
		fi
	elif [[ $src == "default" ]]; then
		mntpt="/$dataset"
	else
		# Walk back up the hierarchy building up the
		# expected mountpoint property value.
		obj_name=${dataset##*/}

		while [[ $src != $dataset ]]; do
			dset=${dataset%/*}

			mnt_val=`get_prop mountpoint $dset`

			mod_prop_val=${mnt_val##*/}
			new_path="/"$mod_prop_val$new_path
			dataset=$dset
		done

		mntpt=$new_path"/"$obj_name
	fi
	echo $mntpt
}

#
# Simple function to verify that a property has the
# expected value.
#
function verify_prop_val #property dataset src index
{
	typeset prop=$1
	typeset dataset=$2
	typeset src=$3
	typeset idx=$4
	typeset new_path=""
	typeset dset
	typeset exp_val
	typeset prop_val

	prop_val=`get_prop $prop $dataset`

	# mountpoint property is handled as a special case
	if [[ $prop == "mountpoint" ]]; then
		exp_val=`get_mntpt_val $dataset $src $idx`
	else
		if [[ $src == "local" ]]; then
			exp_val=${local_val[idx]}
		elif [[ $src == "default" ]]; then
			exp_val=${def_val[idx]}
		else
			#
			# We are inheriting the value from somewhere
			# up the hierarchy.
			#
			exp_val=`get_prop $prop $src`
		fi
	fi

	if [[ $prop_val != $exp_val ]]; then
		# After putback PSARC/2008/231 Apr,09,2008,
		# the default value of aclinherit has changed to be
		# 'restricted' instead of 'secure',
		# but the old interface of 'secure' still exist

		if [[ $prop != "aclinherit" || \
		    $exp_val != "secure" || \
		    $prop_val != "restricted" ]]; then

			log_fail "$prop of $dataset is [$prop_val] rather "\
			    "than [$exp_val]"
		fi
	fi
}

#
# Function to read the configX.cfg files and create the specified
# dataset hierarchy
#
function scan_config { #config-file
	typeset config_file=$1

	DISK=${DISKS%% *}

	list=""
	typeset -i mount_dir=1

	grep "^[^#]" $config_file | {
		while read name type init ; do
			create_dataset $name $type $DISK
			init_props $name $init $mount_dir
			((mount_dir = mount_dir + 1))
		done
	}
}

#
# Function to check an exit flag, calling log_fail if that exit flag
# is non-zero. Can be used from code that runs in a tight loop, which
# would otherwise result in a lot of journal output.
#
function check_failure { # int status, error message to use

	typeset -i exit_flag=$1
	error_message=$2

	if [[ $exit_flag -ne 0 ]]; then
		log_fail "$error_message"
	fi
}


#
# Main function. Executes the commands specified in the stateX.cfg
# files and then verifies that all the properties have the correct
# values and 'source' fields.
#
function scan_state { #state-file
	typeset state_file=$1
	typeset -i i=0
	typeset -i j=0

	log_note "Reading state from $state_file"

	while ((i <  ${#prop[*]})); do
		grep "^[^#]" $state_file | {
			while IFS=: read target op; do
				#
				# The user can if they wish specify that no
				# operation be performed (by specifying '-'
				# rather than a command). This is not as
				# useless as it sounds as it allows us to
				# verify that the dataset hierarchy has been
				# set up correctly as specified in the
				# configX.cfg file (which includes 'set'ting
				# properties at a higher level and checking
				# that they propagate down to the lower levels.
				#
				# Note in a few places here, we use
				# check_failure, rather than log_must - this
				# substantially reduces journal output.
				#
				if [[ $op == "-" ]]; then
					log_note "No operation specified"
				else
					export __ZFS_POOL_RESTRICT="TESTPOOL"
					log_must_busy zfs unmount -a
					unset __ZFS_POOL_RESTRICT

					for p in ${prop[i]} ${prop[((i+1))]}; do
						zfs $op $p $target
						check_failure $? "zfs $op $p $target"
					done
				fi
				for check_obj in $list; do
					read init_src final_src

					for p in ${prop[i]} ${prop[((i+1))]}; do
					# check_failure to keep journal small
						verify_prop_src $check_obj $p \
						    $final_src
						check_failure $? "verify" \
						    "_prop_src $check_obj $p" \
						    "$final_src"

					# Again, to keep journal size down.
						verify_prop_val $p $check_obj \
						    $final_src $j
						check_failure $? "verify" \
						    "_prop_val $check_obj $p" \
						    "$final_src"
					done
				done
			done
		}
		((i = i + 2))
		((j = j + 1))
	done
}

#
# Note that we keep this list relatively short so that this test doesn't
# time out (after taking more than 10 minutes).
#
set -A prop "checksum" "" \
	"compression" "" \
	"atime" "" \
	"sharenfs" "" \
	"recordsize" "recsize" \
	"snapdir" "" \
	"readonly" "" \
	"redundant_metadata" ""

#
# Note except for the mountpoint default value (which is handled in
# the routine itself), each property specified in the 'prop' array
# above must have a corresponding entry in the two arrays below.
#

set -A def_val "on" "on" "on" \
	"off" "" \
	"hidden" \
	"off" \
	"all"

set -A local_val "off" "off" "off" \
	"on" "" \
	"visible" \
	"off" \
	"none"

#
# Add system specific values
#
if is_linux; then
	prop+=("acltype" "")
	def_val+=("off")
	local_val+=("off")
else
	prop+=("aclmode" "")
	def_val+=("discard")
	local_val+=("groupmask")
fi
if is_illumos; then
	prop+=("mountpoint" "")
	def_val+=("")
	local_val+=("$TESTDIR")
fi

#
# Global flag indicating whether the default record size had been
# read.
#
typeset def_recordsize=0

set -A config_files $(ls $STF_SUITE/tests/functional/inheritance/config*[1-9]*.cfg)
set -A state_files $(ls $STF_SUITE/tests/functional/inheritance/state*.cfg)

#
# Global list of datasets created.
#
list=""

typeset -i k=0

if [[ ${#config_files[*]} != ${#state_files[*]} ]]; then
	log_fail "Must have the same number of config files " \
	    " (${#config_files[*]}) and state files ${#state_files[*]}"
fi

while ((k < ${#config_files[*]})); do
	default_cleanup_noexit
	def_recordsize=0

	log_note "Testing configuration ${config_files[k]}"

	scan_config ${config_files[k]}
	scan_state ${state_files[k]}

	((k = k + 1))
done

log_pass "Properties correctly inherited as expected"
