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
# Copyright (c) 2015 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify compressed send works correctly with datasets of varying recsize.
#
# Strategy:
# 1. Check the recv behavior (into pools with features enabled and disabled)
#    of all combinations of -c -p and -L. Verify the stream is compressed,
#    and that the recsize property and that of a received file is correct
#    according to this matrix:
#
# +---------+--------+------------+------------+-----------+-----------+
# | send    | send   | received   | received   | received  | received  |
# | stream  | stream | file bs    | prop       | file bs   | props     |
# | recsize | flags  | (disabled) | (disabled) | (enabled) | (enabled) |
# +---------+--------+------------+------------+-----------+-----------+
# |    128k |        |       128k |       128k |      128k |      128k |
# |    128k |     -c |      Fails |      Fails |      128k |      128k |
# |    128k |     -p |       128k |       128k |      128k |      128k |
# |    128k |     -L |       128k |       128k |      128k |      128k |
# |    128k |    -cp |      Fails |      Fails |      128k |      128k |
# |    128k |    -cL |      Fails |      Fails |      128k |      128k |
# |    128k |    -pL |       128k |       128k |      128k |      128k |
# |    128k |   -cpL |      Fails |      Fails |      128k |      128k |
# |      1m |        |      Fails |      Fails |      128k |      128k |
# |      1m |     -c |      Fails |      Fails |      128k |      128k |
# |      1m |     -p |       128k |       128k |      128k |        1m |
# |      1m |     -L |      Fails |      Fails |        1m |      128k |
# |      1m |    -cp |      Fails |      Fails |      128k |        1m |
# |      1m |    -cL |      Fails |      Fails |        1m |      128k |
# |      1m |    -pL |      Fails |      Fails |        1m |        1m |
# |      1m |   -cpL |      Fails |      Fails |        1m |        1m |
# +---------+--------+------------+------------+-----------+-----------+
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/128k && log_must_busy zfs destroy $TESTPOOL/128k
	datasetexists $TESTPOOL/1m && log_must_busy zfs destroy $TESTPOOL/1m
	cleanup_pool $POOL2
	destroy_pool $POOL3
}

# For a received stream, verify the recsize (prop and file) match expectations.
function check_recsize
{
	typeset recv_ds=$1
	typeset expected_file_bs=$2
	typeset expected_recsize=$3
	typeset file="$(get_prop mountpoint $recv_ds)/testfile"

	[[ -f $file ]] || log_fail "file '$file' doesn't exist"

	typeset read_recsize=$(get_prop recsize $recv_ds)
	if is_freebsd; then
		typeset read_file_bs=$(stat -f "%k" $file)
	else
		typeset read_file_bs=$(stat $file | sed -n \
		    's/.*IO Block: \([0-9]*\).*/\1/p')
	fi

	[[ $read_recsize = $expected_recsize ]] || log_fail \
	    "read_recsize: $read_recsize expected_recsize: $expected_recsize"
	[[ $read_file_bs = $expected_file_bs ]] || log_fail \
	    "read_file_bs: $read_file_bs expected_file_bs: $expected_file_bs"
}

#
# This function does a zfs send and receive according to the parameters
# below, and verifies the data shown in the strategy section.
#
# -[cpL] flags to pass through to 'zfs send'
# -d Receive into a pool with all features disabled
#
# $1 The recordsize of the send dataset
# $2 Whether or not the recv should work.
# $3 The blocksize expected in a received file (default 128k)
# $4 The recordsize property expected in a received dataset (default 128k)
#
function check
{
	typeset recv_pool=$POOL2
	typeset flags='-'

	while getopts "cdpL" opt; do
		case $opt in
		c)
			flags+='c'
			;;
		d)
			recv_pool=$POOL3
			;;
		p)
			flags+='p'
			;;
		L)
			flags+='L'
			;;
		esac
	done
	shift $(($OPTIND - 1))
	[[ ${#flags} -eq 1 ]] && flags=''

	typeset recsize=$1
	typeset verify=$2
	typeset expected_file_bs=${3-131072}
	typeset expected_recsize=${4-131072}
	typeset send_ds=$TESTPOOL/$recsize
	typeset send_snap=$send_ds@snap
	typeset recv_ds=$recv_pool/$recsize
	typeset stream=$BACKDIR/stream.out

	datasetexists $send_ds || log_fail "send ds: $send_ds doesn't exist"
	[[ -f $stream ]] && log_must rm $stream
	log_must eval "zfs send $flags $send_snap >$stream"
	$verify eval "zfs recv $recv_ds <$stream"
	typeset stream_size=$(cat $stream | zstreamdump | sed -n \
	    's/	Total write size = \(.*\) (0x.*)/\1/p')

	#
	# Special case: For a send dataset with large blocks, don't try to
	# verify the stream size is correct if the compress flag is present
	# but the large blocks flag isn't. In these cases, the user data
	# isn't compressed in the stream (though metadata is) so the
	# verification would fail.
	#
	typeset do_size_test=true
	[[ $recsize = $large && $flags =~ 'c' && ! $flags =~ 'L' ]] && \
	    do_size_test=false

	$do_size_test && verify_stream_size $stream $send_ds

	if [[ $verify = "log_mustnot" ]]; then
		datasetnonexists $recv_ds || log_fail "$recv_ds shouldn't exist"
		return
	fi

	check_recsize $recv_ds $expected_file_bs $expected_recsize
	$do_size_test && verify_stream_size $stream $recv_ds
	log_must_busy zfs destroy -r $recv_ds
}

log_assert "Verify compressed send works with datasets of varying recsize."
log_onexit cleanup
typeset recsize opts dir
typeset small=$((128 * 1024))
typeset large=$((1024 * 1024))

# Create POOL3 with features disabled and datasets to create test send streams
datasetexists $POOL3 && log_must zpool destroy $POOL3
log_must zpool create -d $POOL3 $DISK3
write_compressible $BACKDIR 32m
for recsize in $small $large; do
	log_must zfs create -o compress=gzip -o recsize=$recsize \
	    $TESTPOOL/$recsize
	dir=$(get_prop mountpoint $TESTPOOL/$recsize)
	log_must cp $BACKDIR/file.0 $dir/testfile
	log_must zfs snapshot $TESTPOOL/$recsize@snap
done

# Run tests for send streams without large blocks
for opts in '' -d -c -p -dp -L -dL -cp -cL -pL -dpL -cpL; do
	check $opts $small log_must
done
for opts in -dc -dcp -dcL -dcpL; do
	check $opts $small log_mustnot
done

# Run tests for send streams with large blocks
for opts in '' -d -dp -c; do
	check $opts $large log_must
done
for opts in -dc -dL -dcp -dcL -dpL -dcpL; do
	check $opts $large log_mustnot
done
check -p $large log_must $small $large
check -L $large log_must $large $small
check -cp $large log_must $small $large
check -cL $large log_must $large $small
check -pL $large log_must $large $large
check -cpL $large log_must $large $large

log_pass "Compressed send works with datasets of varying recsize."
