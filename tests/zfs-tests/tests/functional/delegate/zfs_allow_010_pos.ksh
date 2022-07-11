#!/bin/ksh -p
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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/delegate/delegate_common.kshlib

#
# DESCRIPTION:
#	Scan the following permissions one by one to verify privileged user
#	has correct permission delegation in datasets.
#
# STRATEGY:
#	1. Delegate all the permission one by one to user on dataset.
#	2. Verify privileged user has correct permission without any other
#	   permissions allowed.
#

verify_runnable "both"

log_assert "Verify privileged user has correct permissions once which was "\
	"delegated to him in datasets"
log_onexit restore_root_datasets

if is_linux; then
#
#				Results in	Results in
#		Permission	Filesystem	Volume
#
# Removed for Linux:
# - mount	- mount(8) does not permit non-superuser mounts
# - mountpoint	- mount(8) does not permit non-superuser mounts
# - canmount	- mount(8) does not permit non-superuser mounts
# - rename      - mount(8) does not permit non-superuser mounts
# - zoned	- zones are not supported
# - destroy     - umount(8) does not permit non-superuser umounts
# - sharenfs	- sharing requires superuser privileges
# - share	- sharing requires superuser privileges
# - readonly	- mount(8) does not permit non-superuser remounts
#
set -A perms	create		true		false	\
		snapshot	true		true	\
		send		true		true	\
		allow		true		true	\
		quota		true		false	\
		reservation	true		true	\
		dnodesize	true		false	\
		recordsize	true		false	\
		checksum	true		true	\
		compression	true		true	\
		atime		true		false	\
		devices		true		false	\
		exec		true		false	\
		volsize		false		true	\
		setuid		true		false	\
		snapdir		true		false	\
		userprop	true		true	\
		aclinherit	true		false	\
		rollback	true		true	\
		clone		true		true	\
		promote		true		true	\
		xattr		true		false	\
		receive		true		false

elif is_freebsd; then
#				Results in	Results in
#		Permission	Filesystem	Volume
#
# Removed for FreeBSD
# - jailed	- jailing requires superuser privileges
# - sharenfs	- sharing requires superuser privileges
# - share	- sharing requires superuser privileges
# - xattr	- Not supported on FreeBSD
#
set -A perms	create		true		false	\
		snapshot	true		true	\
		mount		true		false	\
		send		true		true	\
		allow		true		true	\
		quota		true		false	\
		reservation	true		true	\
		dnodesize	true		false	\
		recordsize	true		false	\
		mountpoint	true		false	\
		checksum	true		true	\
		compression	true		true	\
		canmount	true		false	\
		atime		true		false	\
		devices		true		false	\
		exec		true		false	\
		volsize		false		true	\
		setuid		true		false	\
		readonly	true		true	\
		snapdir		true		false	\
		userprop	true		true	\
		aclmode		true		false	\
		aclinherit	true		false	\
		rollback	true		true	\
		clone		true		true	\
		rename		true		true	\
		promote		true		true	\
		receive		true		false   \
		destroy		true		true

else

set -A perms	create		true		false	\
		snapshot	true		true	\
		mount		true		false	\
		send		true		true	\
		allow		true		true	\
		quota		true		false	\
		reservation	true		true	\
		dnodesize	true		false	\
		recordsize	true		false	\
		mountpoint	true		false	\
		checksum	true		true	\
		compression	true		true	\
		canmount	true		false	\
		atime		true		false	\
		devices		true		false	\
		exec		true		false	\
		volsize		false		true	\
		setuid		true		false	\
		readonly	true		true	\
		snapdir		true		false	\
		userprop	true		true	\
		aclmode		true		false	\
		aclinherit	true		false	\
		rollback	true		true	\
		clone		true		true	\
		rename		true		true	\
		promote		true		true	\
		zoned		true		false	\
		xattr		true		false	\
		receive		true		false	\
		destroy		true		true

if is_global_zone; then
	typeset -i n=${#perms[@]}
	perms[((n))]="sharenfs"; perms[((n+1))]="true"; perms[((n+2))]="false"
	perms[((n+3))]="share"; perms[((n+4))]="true"; perms[((n+5))]="false"
fi
fi

for dtst in $DATASETS; do
	typeset -i k=1
	typeset type=$(get_prop type $dtst)
	[[ $type == "volume" ]] && k=2

	typeset -i i=0
	while (( i < ${#perms[@]} )); do
		log_must zfs allow $STAFF1 ${perms[$i]} $dtst

		if [[ ${perms[((i+k))]} == "true" ]]; then
			log_must verify_perm $dtst ${perms[$i]} $STAFF1
		else
			log_must verify_noperm $dtst ${perms[$i]} $STAFF1
		fi

		log_must restore_root_datasets

		((i += 3))
	done
done

log_pass "Verify privileged user has correct permissions " \
	"in datasets passed."
