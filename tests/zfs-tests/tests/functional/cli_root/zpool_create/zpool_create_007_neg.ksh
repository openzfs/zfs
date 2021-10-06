#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
# 'zpool create' should return an error with badly formed parameters.
#
# STRATEGY:
# 1. Create an array of parameters
# 2. For each parameter in the array, execute 'zpool create'
# 3. Verify an error is returned.
#

verify_runnable "global"

typeset object_store_params="-o object-endpoint=$ZTS_OBJECT_ENDPOINT \
	-o object-region=$ZTS_REGION \
	-o object-credentials-profile=$ZTS_CREDS_PROFILE"


if use_object_store; then
	set -A args  "" "-?" "-n" "-f" "-nf" "-fn" "-f -n" "--f" "-e" "-s" \
		"-m" "-R" "-m -R" "-Rm" "-mR" "-m $TESTDIR $TESTPOOL" \
		"-R $TESTDIR $TESTPOOL" \
		"-m nodir $object_store_params $TESTPOOL s3 $ZTS_BUCKET_NAME" \
		"-R nodir $object_store_params $TESTPOOL s3 $ZTS_BUCKET_NAME" \
		"-m nodir -R nodir $object_store_params $TESTPOOL s3 \
		$ZTS_BUCKET_NAME" \
		"-R nodir -m nodir $object_store_params $TESTPOOL s3 \
		$ZTS_BUCKET_NAME" \
		"-R $TESTDIR -m nodir $object_store_params $TESTPOOL s3 \
		$ZTS_BUCKET_NAME" \
		"-R nodir -m $TESTDIR $object_store_params $TESTPOOL s3 \
		$ZTS_BUCKET_NAME" \
		"-blah" "$TESTPOOL" "$TESTPOOL blah" "$TESTPOOL c?t0d0" \
		"$TESTPOOL c0txd0" "$TESTPOOL c0t0dx" "$TESTPOOL cxtxdx" \
		"$object_store_params 1tank s3 $ZTS_BUCKET_NAME" \
		"$object_store_params 1234 s3 $ZTS_BUCKET_NAME" \
		"$object_store_params ?tank s3 $ZTS_BUCKET_NAME" \
		"$object_store_params tan%k s3 $ZTS_BUCKET_NAME" \
		"$object_store_params ta@# s3 $ZTS_BUCKET_NAME" \
		"$object_store_params tan+k s3 $ZTS_BUCKET_NAME" \
		"$object_store_params $BYND_MAX_NAME s3 $ZTS_BUCKET_NAME" \
		"$object_store_params $TESTPOOL s3" \
		"$object_store_params $TESTPOOL $ZTS_BUCKET_NAME" \
		"-o object-endpoint=$ZTS_OBJECT_ENDPOINT \
		-o object-credentials-profile=$ZTS_CREDS_PROFILE \
		$TESTPOOL s3 $ZTS_BUCKET_NAME" \
		"-o object-region=$ZTS_REGION \
		-o object-credentials-profile=$ZTS_CREDS_PROFILE \
		$TESTPOOL s3 $ZTS_BUCKET_NAME" \
		"-o object-endpoint=$ZTS_OBJECT_ENDPOINT \
		-o object-region=$ZTS_REGION \
		-o object-credentials-profile=blah \
		$TESTPOOL s3 $ZTS_BUCKET_NAME" \
		"-o object-endpoint=blah \
		-o object-region=$ZTS_REGION \
		-o object-credentials-profile=$ZTS_CREDS_PROFILE \
		$TESTPOOL s3 $ZTS_BUCKET_NAME" \
		"$object_store_params $TESTPOOL s3 blah" \
		"$object_store_params $TESTPOOL s3 $ZTS_BUCKET_NAME mirror" \
		"$object_store_params $TESTPOOL s3 $ZTS_BUCKET_NAME raidz" \
		"$object_store_params $TESTPOOL s3 $ZTS_BUCKET_NAME draid" \
		"$TESTPOOL s3 $ZTS_BUCKET_NAME" \
		"$TESTPOOL $ZTS_BUCKET_NAME"

	# Testing with invalid region is not applicable for minio. Minio ignores
	# object-region value and hence, the command will pass instead of
	# failing. We should add this test case for just AWS S3.
	if endpoint_is_s3; then
		args+=("-o object-endpoint=$ZTS_OBJECT_ENDPOINT \
			-o object-region=blah \
			-o object-credentials-profile=$ZTS_CREDS_PROFILE \
			$TESTPOOL s3 $ZTS_BUCKET_NAME")
	fi
else
	set -A args  "" "-?" "-n" "-f" "-nf" "-fn" "-f -n" "--f" "-e" "-s" \
		"-m" "-R" "-m -R" "-Rm" "-mR" "-m $TESTDIR $TESTPOOL" \
		"-R $TESTDIR $TESTPOOL" "-m nodir $TESTPOOL $DISK0" \
		"-R nodir $TESTPOOL $DISK0" \
		"-m nodir -R nodir $TESTPOOL $DISK0" \
		"-R nodir -m nodir $TESTPOOL $DISK0" \
		"-R $TESTDIR -m nodir $TESTPOOL $DISK0" \
		"-R nodir -m $TESTDIR $TESTPOOL $DISK0" \
		"-blah" "$TESTPOOL" "$TESTPOOL blah" "$TESTPOOL c?t0d0" \
		"$TESTPOOL c0txd0" "$TESTPOOL c0t0dx" "$TESTPOOL cxtxdx" \
		"$TESTPOOL mirror" "$TESTPOOL raidz" "$TESTPOOL mirror raidz" \
		"$TESTPOOL raidz1" "$TESTPOOL mirror raidz1" \
		"$TESTPOOL draid1" "$TESTPOOL mirror draid1" \
		"$TESTPOOL mirror c?t?d?" "$TESTPOOL mirror $DISK0 c0t1d?" \
		"$TESTPOOL RAIDZ $DISK0 $DISK1" \
		"$TESTPOOL $DISK0 log $DISK1 log $DISK2" \
		"$TESTPOOL $DISK0 spare $DISK1 spare $DISK2" \
		"$TESTPOOL RAIDZ1 $DISK0 $DISK1" "$TESTPOOL MIRROR $DISK0" \
		"$TESTPOOL DRAID $DISK1 $DISK2 $DISK3" \
		"$TESTPOOL raidz $DISK0" \
		"$TESTPOOL raidz1 $DISK0" "$TESTPOOL draid $DISK0" \
		"$TESTPOOL draid2 $DISK0 $DISK1" \
		"$TESTPOOL draid $DISK0 $DISK1 $DISK2 spare s0-draid1-0" \
		"1tank $DISK0" "1234 $DISK0" "?tank $DISK0" \
		"tan%k $DISK0" "ta@# $DISK0" "tan+k $DISK0" \
		"$BYND_MAX_NAME $DISK0"
fi

log_assert "'zpool create' should return an error with badly-formed parameters."
log_onexit default_cleanup_noexit

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_mustnot zpool create ${args[i]}
	((i = i + 1))
done

log_pass "'zpool create' with badly formed parameters failed as expected."
