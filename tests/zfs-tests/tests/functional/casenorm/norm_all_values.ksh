#!/bin/ksh -p
#
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
# Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/casenorm/casenorm.kshlib

# DESCRIPTION:
# Check that we can create FS with all supported normalization forms.
#
# STRATEGY:
# 1. Create FS with all supported normalization forms.
# 2. Check that utf8only is set on except for normalization=none.
# 3. Check that it's not possible to create FS with utf8only=off
#    and normalization other than none.

verify_runnable "global"

function cleanup
{
	destroy_testfs
}

log_onexit cleanup
log_assert "Can create FS with all supported normalization forms"

for form in none formC formD formKC formKD; do
	create_testfs "-o normalization=$form"
	if [[ $form != "none" ]] ; then
		utf8only=$(zfs get -H -o value utf8only $TESTPOOL/$TESTFS)
		if [[ $utf8only != "on" ]]; then
			log_fail "Turning on normalization didn't set " \
			    "utf8only to on"
		fi
	fi
	destroy_testfs
done

for form in formC formD formKC formKD; do
	log_mustnot zfs create -o utf8only=off -o normalization=$form \
	    $TESTPOOL/$TESTFS
	destroy_testfs
done

for form in formC formD formKC formKD; do
	create_testfs "-o normalization=$form"
	log_must zfs create -o utf8only=off $TESTPOOL/$TESTFS/$TESTSUBFS
	normalization=$(zfs get -H -o value normalization $TESTPOOL/$TESTFS/$TESTSUBFS)
	if [[ $normalization != "none" ]]; then
		log_fail "Turning off utf8only didn't set normalization to none"
	fi
	log_must zfs destroy $TESTPOOL/$TESTFS/$TESTSUBFS
	destroy_testfs
done

log_pass "Can create FS with all supported normalization forms"
