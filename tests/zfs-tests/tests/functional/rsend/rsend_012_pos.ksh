#!/bin/ksh -p
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
# Copyright (c) 2009, Sun Microsystems Inc. All rights reserved.
# Copyright (c) 2013, 2016, Delphix. All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/properties.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
#	zfs send -R will backup all the filesystem properties correctly.
#
# STRATEGY:
#	1. Setting properties for all the filesystem and volumes randomly
#	2. Backup all the data from POOL by send -R
#	3. Restore all the data in POOL2
#	4. Verify all the properties in the two pools are the same
#

verify_runnable "global"

function edited_prop
{
	typeset behaviour=$1
	typeset ds=$2
	typeset backfile=$TESTDIR/edited_prop_$ds
	typeset te=0

	case $behaviour in
		"get")
			is_te_enabled && te=1
			typeset props=$(zfs inherit 2>&1 | \
				awk -v te=$te '$2=="YES" && $1 !~ /^vol|\.\.\.$/ && (te || $1 != "mlslabel") {printf("%s,", $1)}')
			log_must eval "zfs get -Ho property,value ${props%,} $ds >> $backfile"
			;;
		"set")
			if [[ ! -f $backfile ]] ; then
				log_fail "$ds need backup properties firstly."
			fi

			log_must zfs set $(tr '\t' '=' < $backfile) "$ds"
			;;
		*)
			log_fail "Unrecognized behaviour: $behaviour"
	esac
}

function cleanup
{
	log_must cleanup_pool $POOL
	log_must cleanup_pool $POOL2

	log_must edited_prop "set" $POOL
	log_must edited_prop "set" $POOL2

	typeset prop
	for prop in $(fs_inherit_prop) ; do
		log_must zfs inherit $prop $POOL
		log_must zfs inherit $prop $POOL2
	done

	log_must setup_test_model $POOL

	if [[ -d $TESTDIR ]]; then
		log_must rm -rf $TESTDIR/*
	fi
}

log_assert "Verify zfs send -R will backup all the filesystem properties " \
	"correctly."
log_onexit cleanup

log_must edited_prop "get" $POOL
log_must edited_prop "get" $POOL2

for fs in "$POOL" "$POOL/pclone" "$POOL/$FS" "$POOL/$FS/fs1" \
	"$POOL/$FS/fs1/fs2" "$POOL/$FS/fs1/fclone" ; do
	rand_set_prop $fs aclinherit "discard" "noallow" "secure" "passthrough"
	rand_set_prop $fs checksum "on" "off" "fletcher2" "fletcher4" "sha256"
	rand_set_prop $fs acltype "off" "posix" "nfsv4" "noacl" "posixacl"
	rand_set_prop $fs atime "on" "off"
	rand_set_prop $fs checksum "on" "off" "fletcher2" "fletcher4" "sha256"
	rand_set_prop $fs compression "${compress_prop_vals[@]}"
	rand_set_prop $fs copies "1" "2" "3"
	rand_set_prop $fs devices "on" "off"
	rand_set_prop $fs exec "on" "off"
	rand_set_prop $fs quota "512M" "1024M"
	rand_set_prop $fs recordsize "512" "2K" "8K" "32K" "128K"
	rand_set_prop $fs dnodesize "legacy" "auto" "1k" "2k" "4k" "8k" "16k"
	rand_set_prop $fs setuid "on" "off"
	rand_set_prop $fs snapdir "hidden" "visible"
	if ! is_freebsd; then
		rand_set_prop $fs xattr "on" "off"
	fi
	rand_set_prop $fs user:prop "aaa" "bbb" "23421" "()-+?"
done

for vol in "$POOL/vol" "$POOL/$FS/vol" ; do
	rand_set_prop $vol checksum "on" "off" "fletcher2" "fletcher4" "sha256"
	rand_set_prop $vol compression "${compress_prop_vals[@]}"
	rand_set_prop $vol readonly "on" "off"
	rand_set_prop $vol copies "1" "2" "3"
	rand_set_prop $vol user:prop "aaa" "bbb" "23421" "()-+?"
done


# Verify inherited property can be received
rand_set_prop $POOL redundant_metadata "all" "most"
rand_set_prop $POOL sync "standard" "always" "disabled"

#
# Duplicate POOL2 for testing
#
log_must eval "zfs send -R $POOL@final > $BACKDIR/pool-final-R"
log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/pool-final-R"

#
# Define all the POOL/POOL2 datasets pair
#
set -A pair 	"$POOL" 		"$POOL2" 		\
		"$POOL/$FS" 		"$POOL2/$FS" 		\
		"$POOL/$FS/fs1"		"$POOL2/$FS/fs1"	\
		"$POOL/$FS/fs1/fs2"	"$POOL2/$FS/fs1/fs2"	\
		"$POOL/pclone"		"$POOL2/pclone"		\
		"$POOL/$FS/fs1/fclone"	"$POOL2/$FS/fs1/fclone" \
		"$POOL/vol"		"$POOL2/vol" 		\
		"$POOL/$FS/vol"		"$POOL2/$FS/vol"

typeset -i i=0
while ((i < ${#pair[@]})); do
	log_must cmp_ds_prop ${pair[$i]} ${pair[((i+1))]} nosource
	((i += 2))
done


i=0
while ((i < ${#pair[@]})); do
	log_must cmp_ds_prop ${pair[$i]}@final ${pair[((i+1))]}@final
	((i += 2))
done

log_pass "Verify zfs send -R will backup all the filesystem properties" \
	"correctly."
