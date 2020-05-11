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
# Copyright (c) 2017 by Delphix. All rights reserved.
# Copyright (c) 2020 by Lawrence Livermore National Security LLC.

. $STF_SUITE/include/libtest.shlib


#
# DESCRIPTION:
#	Create a pool and populate it with files of various
#	recordsizes
#
# STRATEGY:
#	1. Create pool
#	2. Populate it
#	3. Run zdb -Pbbb on pool
#	4. Verify variance on blocksizes
#
function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

SPA_MAXBLOCKSHIFT=24

function histo_populate_test_pool
{
	if [ $# -ne 1 ]; then
		log_note "histo_populate_test_pool: insufficient parameters"
		log_fail "hptp: 1 requested $# received"
	fi
	typeset pool=$1

	set -A recordsizes
	typeset -i min_rsbits=9 #512
	typeset -i max_rsbits=SPA_MAXBLOCKSHIFT #16 MiB
	typeset -i sum_filesizes=0
	re_number='^[0-9]+$'

	let histo_pool_size=$(get_pool_prop size ${pool})
	if [[ ! ${histo_pool_size} =~ ${re_number} ]]; then
		log_fail "histo_pool_size is not numeric ${pool_size}"
	fi
	let max_pool_record_size=$(get_prop recordsize ${pool})
	if [[ ! ${max_pool_record_size} =~ ${re_number} ]]; then
		log_fail "hptp: max_pool_record_size is not numeric ${max_pool_record_size}"
	fi

	sum_filesizes=$(echo "2^21"|bc)
	((min_pool_size=12*sum_filesizes))
	if [ ${histo_pool_size} -lt ${min_pool_size} ]; then
		log_note "hptp: Your pool size ${histo_pool_size}"
		log_fail "hptp: is less than minimum ${min_pool_size}"
	fi
	this_ri=min_rsbits
	file_num=0
	total_count=0
	###################
	# generate 10% + 20% + 30% + 31% = 91% of the filespace
	# attempting to use 100% will lead to no space left on device
	# Heuristic testing showed that 91% was the practical upper
	# bound on the default 4G zpool (mirrored) that is used in
	# testing.
	#
	# In order to expedite testing, we will only fill 2G (of 4G)
	# of the test pool.  You may want to modify this for
	# standalone testing.
	# 
	# In filling only 50% of the pool, we create one object on
	# each "pass" below to achieve multiple objects per record
	# size.  Creating one file per object would lead to 
	# excessive file creation time.
	###################
	# for pass in 10 20 30 31  # 91%
	for pass in 20 20 10 # 50%
	do
		((thiscount=(((histo_pool_size*pass)/100)/sum_filesizes)))

		((total_count+=thiscount))
		for rb in $(seq ${min_rsbits} ${max_rsbits})
		do
			this_rs=$(echo "2^${rb}" | bc)
			if [ ${this_rs} -gt ${max_pool_record_size} ]; then
				continue
			fi
	
			if [ ! -d /${pool}/B_${this_rs} ]; then
				zfs create ${pool}/B_${this_rs}
				zfs set recordsize=${this_rs} \
				    ${pool}/B_${this_rs}
			fi
			####################
			# Create the files in the devices and datasets
			# of the right size.  The files are filled
			# with random data to defeat the compression
			#
			# Note that the dd output is suppressed unless
			# there are errors
			####################

			dd if=/dev/urandom \
			    of=/${pool}/B_${this_rs}/file_${filenum} \
			    bs=${this_rs} count=${thiscount} \
			    iflag=fullblock 2>&1 | \
			    egrep -v -e "records in" -e "records out" \
				-e "bytes.*copied"
			((filenum+=1))
		done
	done

	####################
	# Testing showed that on some devices, unless the pool is 
	# synchronized, that the block counts will be below the 
	# anticipated sizes since not all of the blocks will be flushed
	# to the device.  This 'sync' command prevents that from 
	# happening.
	####################
	log_must zpool sync ${pool}
}
function histo_check_test_pool
{
	if [ $# -ne 1 ]; then
		log_note "histo_check_test_pool: insufficient parameters"
		log_fail "hctp: 1 requested $# received"
	fi	
	typeset pool=$1

	set -A recordsizes
	set -A recordcounts
	typeset -i rb
	typeset -i min_rsbits=9 #512
	typeset -i max_rsbits=SPA_MAXBLOCKSHIFT+1
	typeset -i this_rs
	typeset -i this_ri
	typeset -i sum_filesizes=0
	typeset dumped
	typeset stripped

	let histo_check_pool_size=$(get_pool_prop size ${pool})
	if [[ ! ${histo_check_pool_size} =~ ${re_number} ]]; then
		log_fail "histo_check_pool_size is not numeric ${histo_check_pool_size}"
	fi
	let max_pool_record_size=$(get_prop recordsize ${pool})
	if [[ ! ${max_pool_record_size} =~ ${re_number} ]]; then
		log_fail "hctp: max_pool_record_size is not numeric ${max_pool_record_size}"
	fi

	dumped="${TEST_BASE_DIR}/${pool}_dump.txt"
	stripped="${TEST_BASE_DIR}/${pool}_stripped.txt"

	zdb -Pbbb ${pool} | \
	    tee ${dumped} | \
	    sed -e '1,/^block[ 	][ 	]*psize[ 	][ 	]*lsize.*$/d' \
	    -e '/^size[ 	]*Count/d' -e '/^$/,$d' \
	    > ${stripped}

	sum_filesizes=$(echo "2^21"|bc)

	###################
	# generate 10% + 20% + 30% + 31% = 91% of the filespace
	# attempting to use 100% will lead to no space left on device
	# attempting to use 100% will lead to no space left on device
	# Heuristic testing showed that 91% was the practical upper
	# bound on the default 4G zpool (mirrored) that is used in
	# testing.
	#
	# In order to expedite testing, we will only fill 2G (of 4G)
	# of the test pool.  You may want to modify this for
	# standalone testing.
	# 
	# In filling only 50% of the pool, we create one object on
	# each "pass" below to achieve multiple objects per record
	# size.  Creating one file per object would lead to 
	# excessive file creation time.
	###################
	# for pass in 10 20 30 31  # 91%
	for pass in 20 20 10 # 50%
	do
		((thiscount=(((histo_check_pool_size*pass)/100)/sum_filesizes)))

		for rb in $(seq ${min_rsbits} ${max_rsbits})
		do
			blksize=$(echo "2^$rb"|bc)
			if [ $blksize -le $max_pool_record_size ]; then
				((recordcounts[$blksize]+=thiscount))
			fi
		done
	done

	###################
	# compare the above computed counts for blocks against
	# lsize count.  Since some devices have a minimum hardware
	# blocksize > 512, we cannot compare against the asize count.
	# E.G., if the HWBlocksize = 4096, then the asize counts for
	# 512, 1024 and 2048 will be zero and rolled up into the 
	# 4096 blocksize count for asize.   For verification we stick
	# to just lsize counts.
	#
	# The max_variance is hard-coded here at 10%.  testing so far
	# has shown this to be in the range of 2%-8% so we leave a
	# generous allowance... This might need changes in the future
	###################
	let max_variance=10
	let fail_value=0
	let error_count=0
	log_note "Comparisons for ${pool}"
	log_note "Bsize is the blocksize, Count is predicted value"
	log_note "Bsize\tCount\tpsize\tlsize\tasize"
	while read -r blksize pc pl pm lc ll lm ac al am
	do
		if [ $blksize -gt $max_pool_record_size ]; then
			continue
		fi
		log_note \
		    "$blksize\t${recordcounts[${blksize}]}\t$pc\t$lc\t$ac"

		###################
		# get the computer record count and compute the
		# difference percentage in integer arithmetic
		###################
		rc=${recordcounts[${blksize}]}
		((rclc=(rc-lc)<0?lc-rc:rc-lc)) # absolute value
		((dp=(rclc*100)/rc))

		###################
		# Check against the allowed variance
		###################
		if [ $dp -gt ${max_variance} ]; then
			log_note \
			"Expected variance < ${max_variance}% observed ${dp}%"
			if [ ${dp} -gt ${fail_value} ]; then
				fail_value=${dp}
				((error_count++))
			fi
		fi
	done < ${stripped}
	if [ ${fail_value} -gt 0 ]; then
		if [ ${error_count} -eq 1 ]; then
			log_note "hctp: There was ${error_count} error"
		else
			log_note "hctp:There were a total of ${error_count} errors"
		fi
		log_fail \
		"hctp: Max variance of ${max_variance}% exceeded, saw ${fail_value}%"
	fi
}

log_assert "Verify zdb -Pbbb (block histogram) works as expected"
log_onexit cleanup
verify_runnable "global"
verify_disk_count "$DISKS" 2

default_mirror_setup_noexit $DISKS

histo_populate_test_pool $TESTPOOL

histo_check_test_pool $TESTPOOL

log_pass "Histogram for zdb"
