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
. $STF_SUITE/tests/functional/rsend/rsend.kshlib
. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

#
# Description:
# A raw receive into an encrypted dataset leaves user accounting for
# later. Mounting and reading the received dataset must not write
# it, so the next incremental receive needs no -F. The first write,
# a read of userused@, or zfs userspace starts the accounting
# upgrade, which must then build the accounting. A read of a quota
# such as userquota@ does not start it, and neither does a channel
# program. The same holds after an online receive. A clone of a
# received snapshot, and a received dataset that differs from its
# latest snapshot, start the upgrade at mount. A plain stream
# received under an encrypted parent arrives with its accounting,
# so for that copy the test only checks that the mount and an
# incremental receive write nothing.
#
# Strategy:
# 1. Create an encrypted dataset with 1000 files and snapshot it.
# 2. Receive it raw, and as a plain stream under an encrypted
#    parent, both with atime=off.
# 3. Mount both, read every file, run sync, and check written is
#    0.
# 4. Receive an incremental into each without -F, and check
#    written is still 0.
# 5. Remount the raw copy. userquota@root and userobjquota@root
#    must read "-", and written must stay 0.
# 6. Read userused@0 of the raw copy with zfs.get_prop in a
#    channel program, in sync mode and with -n. Both must fail
#    with ENOTSUP, and written must stay 0.
# 7. Read userused@root. The upgrade must finish and build userobj
#    accounting, and written must go above 0.
# 8. Receive a second raw copy, mount it and append to a file.
#    Creating a file would also start the upgrade from
#    zfs_id_overobjquota(). The upgrade must finish and build
#    userobj accounting.
# 9. Receive a third raw copy and, without writing it, run zfs
#    userspace on it. Its exit status is not checked, since it
#    does not wait for the upgrade. The upgrade must finish and
#    build userobj accounting, and written must go above 0.
# 10. Receive two more raw copies, mount both, and receive the
#     incremental into each while it is mounted. written must
#     stay 0. zfs userspace on one copy, and an append on the
#     other, must then start the upgrade.
# 11. Clone the first snapshot of the raw copy from step 2. The
#     clone has no snapshot of its own, so its mount must start
#     the upgrade.
# 12. Receive both snapshots raw with -u and destroy the second.
#     The dataset now differs from its latest snapshot, so written
#     is above 0 before the mount, and the mount must start the
#     upgrade.
#

verify_runnable "both"

typeset passphrase="password1"
typeset src=$POOL/mu_src
typeset raw=$POOL/mu_raw
typeset encp=$POOL/mu_encp
typeset plain=$POOL/mu_encp/plain
typeset append=$POOL/mu_append
typeset uspace=$POOL/mu_uspace
typeset rawon=$POOL/mu_rawon
typeset appendon=$POOL/mu_appendon
typeset clone=$POOL/mu_clone
typeset chg=$POOL/mu_chg

function cleanup
{
	for ds in $clone $src $raw $append $uspace $rawon $appendon \
	    $chg $encp; do
		datasetexists $ds && destroy_dataset $ds -r
	done
}

function recv_raw # snapshot dataset
{
	log_must eval "zfs send -w $1 | zfs recv -u -o atime=off $2"
	log_must eval "echo $passphrase | zfs load-key $2"
	log_must zfs mount $2
}

function check_unwritten # dataset ...
{
	typeset ds

	sync_pool $POOL
	sleep 2
	sync_pool $POOL
	for ds in "$@"; do
		log_note "$ds written: $(get_prop written $ds)"
		log_must test "$(get_prop written $ds)" = "0"
	done
}

function check_upgraded # dataset
{
	typeset -i n=0

	# userobjquota@ reads "-" until the upgrade is done, and
	# unlike userobjused@ it does not start the upgrade.
	while [[ "$(get_prop userobjquota@root $1)" == "-" ]]; do
		((n < 60)) || log_fail "$1 accounting was not built"
		sleep 1
		((n++))
	done
	sync_pool $POOL
	log_must test "$(get_prop userobjused@root $1)" -gt 0
	log_must test "$(get_prop written $1)" -gt 0
}

log_assert "Mounting a dataset received into encryption does not" \
    "write it"
log_onexit cleanup

log_must eval "echo $passphrase | zfs create -o encryption=on" \
    "-o keyformat=passphrase $src"
log_must eval "echo $passphrase | zfs create -o encryption=on" \
    "-o keyformat=passphrase $encp"
typeset srcmnt=$(get_prop mountpoint $src)
typeset -i i
for ((i = 1; i <= 1000; i++)); do
	echo $i > $srcmnt/f$i
done
log_must zfs snapshot $src@a

log_must eval "zfs send -w $src@a | zfs recv -u -o atime=off $raw"
log_must eval "echo $passphrase | zfs load-key $raw"
log_must zfs mount $raw
log_must eval "zfs send $src@a | zfs recv -o atime=off $plain"

for ds in $raw $plain; do
	log_must eval "cat $(get_prop mountpoint $ds)/f* >/dev/null"
done
log_must sync
check_unwritten $raw $plain

for ((i = 1001; i <= 1010; i++)); do
	echo $i > $srcmnt/f$i
done
log_must zfs snapshot $src@b
log_must eval "zfs send -w -i @a $src@b | zfs recv $raw"
log_must eval "zfs send -i @a $src@b | zfs recv $plain"
check_unwritten $raw $plain
for ds in $raw $plain; do
	log_must cmp $srcmnt/f1010 $(get_prop mountpoint $ds)/f1010
done

log_must zfs unmount $raw
log_must zfs mount $raw
log_must test "$(get_prop written $raw)" = "0"
log_must test "$(get_prop userquota@root $raw)" = "-"
log_must test "$(get_prop userobjquota@root $raw)" = "-"
check_unwritten $raw
typeset enotsup=95
is_freebsd && enotsup=45
typeset err="unexpected error $enotsup while retrieving property"
log_mustnot_checkerror_program \
    "$err 'userused@0' on dataset '$raw'" $POOL - <<-EOF
	zfs.get_prop("$raw", "userused@0")
EOF
check_unwritten $raw
log_must zfs get userused@root $raw
check_upgraded $raw

recv_raw $src@b $append
check_unwritten $append
log_must eval "echo new >>$(get_prop mountpoint $append)/f1"
check_upgraded $append

recv_raw $src@b $uspace
check_unwritten $uspace
zfs userspace $uspace
check_upgraded $uspace

recv_raw $src@a $rawon
recv_raw $src@a $appendon
log_must eval "zfs send -w -i @a $src@b | zfs recv $rawon"
log_must eval "zfs send -w -i @a $src@b | zfs recv $appendon"
check_unwritten $rawon $appendon
zfs userspace $rawon
check_upgraded $rawon
log_must eval "echo new >>$(get_prop mountpoint $appendon)/f1"
check_upgraded $appendon

log_must zfs clone $raw@a $clone
check_upgraded $clone

log_must eval "zfs send -w $src@a | zfs recv -u $chg"
log_must eval "zfs send -w -i @a $src@b | zfs recv -u $chg"
log_must zfs destroy $chg@b
log_must test "$(get_prop written $chg)" -gt 0
log_must eval "echo $passphrase | zfs load-key $chg"
log_must zfs mount $chg
check_upgraded $chg

log_pass "Mounting a dataset received into encryption does not" \
    "write it"
