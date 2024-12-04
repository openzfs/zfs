#!/bin/ksh -p
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2018 by Datto Inc. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
# Verify that zfs properly handles encryption properties when receiving
# send streams.
#
# STRATEGY:
# 1. Create a few unencrypted and encrypted test datasets with some data
# 2. Take snapshots of these datasets in preparation for sending
# 3. Verify that 'zfs recv -o keylocation=prompt' fails
# 4. Verify that 'zfs recv -x <encryption prop>' fails on a raw send stream
# 5. Verify that encryption properties cannot be changed on incrementals
# 6. Verify that a simple send can be received as an encryption root
# 7. Verify that an unencrypted props send can be received as an
#    encryption root
# 8. Verify that an unencrypted recursive send can be received as an
#    encryption root
# 9. Verify that an unencrypted props send can be received as an
#    encryption child
# 10. Verify that an unencrypted recursive send can be received as an
#     encryption child
#

verify_runnable "both"

function cleanup
{
	destroy_dataset $TESTPOOL/recv "-r"
	destroy_dataset $TESTPOOL/crypt "-r"
	destroy_dataset $TESTPOOL/ds "-r"
	[[ -f $sendfile ]] && log_must rm $sendfile
	[[ -f $keyfile ]] && log_must rm $keyfile
}
log_onexit cleanup

log_assert "'zfs recv' must properly handle encryption properties"

typeset keyfile=/$TESTPOOL/pkey
typeset sendfile=/$TESTPOOL/sendfile
typeset snap=$TESTPOOL/ds@snap1
typeset snap2=$TESTPOOL/ds@snap2
typeset esnap=$TESTPOOL/crypt@snap1
typeset esnap2=$TESTPOOL/crypt@snap2

log_must eval "echo 'password' > $keyfile"

log_must zfs create $TESTPOOL/ds
log_must zfs create $TESTPOOL/ds/ds1

log_must zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=file://$keyfile $TESTPOOL/crypt
log_must zfs create $TESTPOOL/crypt/ds1
log_must zfs create -o keyformat=passphrase -o keylocation=file://$keyfile \
	$TESTPOOL/crypt/ds2

log_must mkfile 1M /$TESTPOOL/ds/$TESTFILE0
log_must cp /$TESTPOOL/ds/$TESTFILE0 /$TESTPOOL/crypt/$TESTFILE0
typeset cksum=$(xxh128digest /$TESTPOOL/ds/$TESTFILE0)

log_must zfs snap -r $snap
log_must zfs snap -r $snap2
log_must zfs snap -r $esnap
log_must zfs snap -r $esnap2

# Embedded data is incompatible with encrypted datasets, so we cannot
# allow embedded streams to be received.
log_note "Must not be able to receive an embedded stream as encrypted"
log_mustnot eval "zfs send -e $TESTPOOL/crypt/ds1 | zfs recv $TESTPOOL/recv"

# We currently don't have an elegant and secure way to pass the passphrase
# in via prompt because the send stream itself is using stdin.
log_note "Must not be able to use 'keylocation=prompt' on receive"
log_must eval "zfs send $snap > $sendfile"
log_mustnot eval "zfs recv -o encryption=on -o keyformat=passphrase" \
	"$TESTPOOL/recv < $sendfile"
log_mustnot eval "zfs recv -o encryption=on -o keyformat=passphrase" \
	"-o keylocation=prompt $TESTPOOL/recv < $sendfile"

# Raw sends do not have access to the decrypted data so we cannot override
# the encryption settings without losing the data.
log_note "Must not be able to disable encryption properties on raw send"
log_must eval "zfs send -w $esnap > $sendfile"
log_mustnot eval "zfs recv -x encryption $TESTPOOL/recv < $sendfile"
log_mustnot eval "zfs recv -x keyformat $TESTPOOL/recv < $sendfile"
log_mustnot eval "zfs recv -x pbkdf2iters $TESTPOOL/recv < $sendfile"

# Encryption properties are set upon creating the dataset. Changing them
# afterwards requires using 'zfs change-key' or an update from a raw send.
log_note "Must not be able to change encryption properties on incrementals"
log_must eval "zfs send $esnap | zfs recv -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=file://$keyfile $TESTPOOL/recv"
log_mustnot eval "zfs send -i $esnap $esnap2 |" \
	"zfs recv -o encryption=aes-128-ccm $TESTPOOL/recv"
log_mustnot eval "zfs send -i $esnap $esnap2 |" \
	"zfs recv -o keyformat=hex $TESTPOOL/recv"
log_mustnot eval "zfs send -i $esnap $esnap2 |" \
	"zfs recv -o pbkdf2iters=100k $TESTPOOL/recv"
log_must zfs destroy -r $TESTPOOL/recv

# Test that we can receive a simple stream as an encryption root.
log_note "Must be able to receive stream as encryption root"
ds=$TESTPOOL/recv
log_must eval "zfs send $snap > $sendfile"
log_must eval "zfs recv -o encryption=on -o keyformat=passphrase" \
	"-o keylocation=file://$keyfile $ds < $sendfile"
log_must test "$(get_prop 'encryption' $ds)" == "aes-256-gcm"
log_must test "$(get_prop 'encryptionroot' $ds)" == "$ds"
log_must test "$(get_prop 'keyformat' $ds)" == "passphrase"
log_must test "$(get_prop 'keylocation' $ds)" == "file://$keyfile"
log_must test "$(get_prop 'mounted' $ds)" == "yes"
recv_cksum=$(xxh128digest /$ds/$TESTFILE0)
log_must test "$recv_cksum" == "$cksum"
log_must zfs destroy -r $ds

# Test that we can override sharesmb property for encrypted raw stream.
log_note "Must be able to override sharesmb property for encrypted raw stream"
ds=$TESTPOOL/recv
log_must eval "zfs send -w $esnap > $sendfile"
log_must eval "zfs recv -o sharesmb=on $ds < $sendfile"
log_must test "$(get_prop 'sharesmb' $ds)" == "on"
log_must zfs destroy -r $ds

# Test that we can override encryption properties on a properties stream
# of an unencrypted dataset, turning it into an encryption root.
log_note "Must be able to receive stream with props as encryption root"
ds=$TESTPOOL/recv
log_must eval "zfs send -p $snap > $sendfile"
log_must eval "zfs recv -o encryption=on -o keyformat=passphrase" \
	"-o keylocation=file://$keyfile $ds < $sendfile"
log_must test "$(get_prop 'encryption' $ds)" == "aes-256-gcm"
log_must test "$(get_prop 'encryptionroot' $ds)" == "$ds"
log_must test "$(get_prop 'keyformat' $ds)" == "passphrase"
log_must test "$(get_prop 'keylocation' $ds)" == "file://$keyfile"
log_must test "$(get_prop 'mounted' $ds)" == "yes"
recv_cksum=$(xxh128digest /$ds/$TESTFILE0)
log_must test "$recv_cksum" == "$cksum"
log_must zfs destroy -r $ds

# Test that we can override encryption properties on a recursive stream
# of an unencrypted dataset, turning it into an encryption root. The root
# dataset of the stream should become an encryption root with all children
# inheriting from it.
log_note "Must be able to receive recursive stream as encryption root"
ds=$TESTPOOL/recv
log_must eval "zfs send -R $snap > $sendfile"
log_must eval "zfs recv -o encryption=on -o keyformat=passphrase" \
	"-o keylocation=file://$keyfile $ds < $sendfile"
log_must test "$(get_prop 'encryption' $ds)" == "aes-256-gcm"
log_must test "$(get_prop 'encryptionroot' $ds)" == "$ds"
log_must test "$(get_prop 'keyformat' $ds)" == "passphrase"
log_must test "$(get_prop 'keylocation' $ds)" == "file://$keyfile"
log_must test "$(get_prop 'mounted' $ds)" == "yes"
recv_cksum=$(xxh128digest /$ds/$TESTFILE0)
log_must test "$recv_cksum" == "$cksum"
log_must zfs destroy -r $ds

# Test that we can override an unencrypted properties stream's encryption
# settings, receiving it as an encrypted child.
log_note "Must be able to receive stream with props to encrypted child"
ds=$TESTPOOL/crypt/recv
log_must eval "zfs send -p $snap > $sendfile"
log_must eval "zfs recv -x encryption $ds < $sendfile"
log_must test "$(get_prop 'encryptionroot' $ds)" == "$TESTPOOL/crypt"
log_must test "$(get_prop 'encryption' $ds)" == "aes-256-gcm"
log_must test "$(get_prop 'keyformat' $ds)" == "passphrase"
log_must test "$(get_prop 'mounted' $ds)" == "yes"
recv_cksum=$(xxh128digest /$ds/$TESTFILE0)
log_must test "$recv_cksum" == "$cksum"
log_must zfs destroy -r $ds

# Test that we can override an unencrypted recursive stream's encryption
# settings, receiving all datasets as encrypted children.
log_note "Must be able to receive recursive stream to encrypted child"
ds=$TESTPOOL/crypt/recv
log_must eval "zfs send -R $snap > $sendfile"
log_must eval "zfs recv -x encryption $ds < $sendfile"
log_must test "$(get_prop 'encryptionroot' $ds)" == "$TESTPOOL/crypt"
log_must test "$(get_prop 'encryption' $ds)" == "aes-256-gcm"
log_must test "$(get_prop 'keyformat' $ds)" == "passphrase"
log_must test "$(get_prop 'mounted' $ds)" == "yes"
recv_cksum=$(xxh128digest /$ds/$TESTFILE0)
log_must test "$recv_cksum" == "$cksum"
log_must zfs destroy -r $ds

# Test that we can override an unencrypted, incremental, recursive stream's
# encryption settings, receiving all datasets as encrypted children.
log_note "Must be able to receive recursive stream to encrypted child"
ds=$TESTPOOL/crypt/recv
log_must eval "zfs send -R $snap2 > $sendfile"
log_must eval "zfs recv -x encryption $ds < $sendfile"
log_must test "$(get_prop 'encryptionroot' $ds)" == "$TESTPOOL/crypt"
log_must test "$(get_prop 'encryption' $ds)" == "aes-256-gcm"
log_must test "$(get_prop 'keyformat' $ds)" == "passphrase"
log_must test "$(get_prop 'mounted' $ds)" == "yes"
recv_cksum=$(xxh128digest /$ds/$TESTFILE0)
log_must test "$recv_cksum" == "$cksum"
log_must zfs destroy -r $ds

# Check that we haven't printed the key to the zpool history log
log_mustnot eval "zpool history -i | grep -q 'wkeydata'"

log_pass "'zfs recv' properly handles encryption properties"
