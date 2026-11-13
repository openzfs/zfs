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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# The kernel terminates the fixed-size strings it gets from userland
# (zfs_cmd_t zc_value, the drr_toname of a receive's BEGIN record) itself,
# and truncates a string that fills its field.
#
# STRATEGY:
# 1. ZFS_IOC_VDEV_SETFRU with a zc_value that fills the field and has no
#    NUL, followed by a marker string in zc_string.  The FRU in the vdev
#    label must be zc_value less its last byte, without the marker, also
#    after an export and import.
# 2. A resumable receive of a truncated stream whose BEGIN record has a
#    drr_toname that fills the field and has no NUL.  The resume token
#    must name the truncated drr_toname.  Abort the receive, then receive
#    the whole stream normally.
#

verify_runnable "global"

VDEV=$TEST_BASE_DIR/unterminated_strings.vdev
STREAM=$TEST_BASE_DIR/unterminated_strings.zsend

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -f $VDEV $STREAM $STREAM.part
}

log_assert "The kernel terminates strings passed in fixed-size fields"
log_onexit cleanup

log_must truncate -s $MINVDEVSIZE $VDEV
log_must zpool create -f $TESTPOOL1 $VDEV

# 1. zc_value is MAXPATHLEN * 2 bytes: the helper prints the size.
typeset guid=$(zpool get -H -o value guid $TESTPOOL1 $VDEV)
typeset valsize
valsize=$(ioctl_unterminated fru $TESTPOOL1 $guid) || \
    log_fail "ioctl_unterminated fru $TESTPOOL1 $guid failed"
typeset -i fru_len=$((valsize - 1))
typeset expected_fru=$(printf "%${fru_len}s" "" | tr ' ' 'f')
log_note "zc_value is $valsize bytes"

# "zpool get fru" truncates long values, so read the vdev label.
function check_fru
{
	log_must zpool sync $TESTPOOL1
	typeset fru=$(zdb -l $VDEV | sed -n "s/^ *fru: '\\(.*\\)'$/\\1/p" | \
	    head -1)

	log_note "FRU is ${#fru} bytes"
	if [[ "$fru" == *zc_string_marker* ]]; then
		log_fail "FRU runs on into zc_string (${#fru} bytes)"
	fi
	if [[ "$fru" != "$expected_fru" ]]; then
		log_fail "FRU is not zc_value truncated to $fru_len bytes" \
		    "(${#fru} bytes)"
	fi
}

check_fru
log_must zpool export $TESTPOOL1
log_must zpool import -d $VDEV $TESTPOOL1
check_fru
log_must eval "zpool status -x $TESTPOOL1 | grep -q 'is healthy'"

# 2. drr_toname is MAXNAMELEN (256) bytes on every platform.
typeset expected_toname=$(printf "%255s" "" | tr ' ' 'n')
log_must zfs create $TESTPOOL1/src
log_must dd if=/dev/urandom of=/$TESTPOOL1/src/file bs=1M count=4
log_must zfs snapshot $TESTPOOL1/src@snap
log_must eval "zfs send $TESTPOOL1/src@snap > $STREAM"
log_must eval "head -c 1048576 $STREAM > $STREAM.part"

typeset out
out=$(ioctl_unterminated recv $TESTPOOL1/dst@snap < $STREAM.part) && \
    log_fail "receive of a truncated stream succeeded"
log_note "$out"
[[ "$out" == "receive: stream truncated" ]] || \
    log_fail "unexpected receive result: $out"
log_must datasetexists $TESTPOOL1/dst
typeset token=$(get_prop receive_resume_token $TESTPOOL1/dst)
if [[ -z "$token" || "$token" == "-" ]]; then
	log_fail "no receive_resume_token on $TESTPOOL1/dst"
fi
log_must eval "zstream token $token > $TEST_BASE_DIR/unterminated_strings.tok"
typeset toname=$(sed -n "s/^ *toname: '\\(.*\\)'$/\\1/p" \
    $TEST_BASE_DIR/unterminated_strings.tok)
rm -f $TEST_BASE_DIR/unterminated_strings.tok
log_note "resume token toname is ${#toname} bytes"
if [[ "$toname" != "$expected_toname" ]]; then
	log_fail "resume token toname is not drr_toname truncated to" \
	    "255 bytes (${#toname} bytes)"
fi

log_must zfs receive -A $TESTPOOL1/dst
log_mustnot datasetexists $TESTPOOL1/dst
log_must eval "zfs receive $TESTPOOL1/dst@snap < $STREAM"
log_must cmp /$TESTPOOL1/src/file /$TESTPOOL1/dst/file

log_pass "The kernel terminates strings passed in fixed-size fields"
