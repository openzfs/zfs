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
# Copyright (c) 2019, 2023 by Klara Inc.  All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zpool prefetch -t ddt <pool>' can successfully load a pool's DDT on demand.
#
# STRATEGY:
# 1. Build up storage pool with deduplicated dataset.
# 2. Export the pool.
# 3. Import the pool, and use zpool prefetch -t ddt to load its table.
# 4. Verify the DDT was loaded successfully using ddt cache stats
#

verify_runnable "both"

log_assert "'zpool prefetch -t ddt <pool>' can successfully load the DDT for a pool."

function getddtstats
{
	typeset -n gds=$1
	typeset pool=$2

	out=$(zpool status -DDp $pool | awk '/^ dedup: / {print $6 " " $9 " " $12}')
	log_note "status -DDp output: ${out}"

	gds.ondisk=$(echo $out | cut -d" " -f1)
	gds.incore=$(echo $out | cut -d" " -f2)
	gds.cached=$(echo $out | cut -d" " -f3)

	# In case of missing data, reset to 0.  This should normally be due
	# to a pool without any DDT.
	[ -z "${gds.ondisk}" ] && gds.ondisk="0"
	[ -z "${gds.incore}" ] && gds.incore="0"
	[ -z "${gds.cached}" ] && gds.cached="0"

	return true
}

# Confirm that nothing happens on a standard pool config.
typeset -A before
log_must getddtstats before $TESTPOOL
log_note "before stats: ${before}"
log_must test "${before.ondisk}" -eq "0"
log_must test "${before.incore}" -eq "0"
log_must test "${before.cached}" -eq "0"
log_must zpool prefetch -t ddt $TESTPOOL

# Build up the deduplicated dataset.  This consists of creating enough files
# to generate a reasonable size DDT for testing purposes.

DATASET=$TESTPOOL/ddt
log_must zfs create -o dedup=on $DATASET
MNTPOINT=$(get_prop mountpoint $TESTPOOL/ddt)

log_note "Generating dataset ..."
typeset -i i=0
while (( i < 16384 )); do
	echo -n $i > $MNTPOINT/f.$i

	# Create some copies of the original mainly for the purpose of
	# having duplicate entries.  About half will have no copies, while
	# the remainder will have an equal distribution of 1-4 copies,
	# depending on the number put into the original.
	typeset -i j
	((j = i % 8))
	while (( j < 4 )); do
		cp $MNTPOINT/f.$i $MNTPOINT/f.$i.$j
		((j += 1))
	done
	((i += 1))
done
log_note "Dataset generation completed."

typeset -A generated
log_must getddtstats generated $TESTPOOL
log_note "generated stats: ${generated}"
log_must test "${generated.ondisk}" -ge "1048576"
log_must test "${generated.incore}" -ge "1048576"
log_must test "${generated.cached}" -ge "1048576"
log_must zpool prefetch -t ddt $TESTPOOL

# Do an export/import series to flush the DDT dataset cache.
typeset -A reimport
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
log_must getddtstats reimport $TESTPOOL
log_note "reimport stats: ${reimport}"
log_must test "${reimport.ondisk}" -ge "1048576"
log_must test "${reimport.incore}" -ge "1048576"
# On reimport, only the first block or two should be cached.
log_must test "${reimport.cached}" -le "65536"

# Finally, reload it and check again.
typeset -A reloaded
log_must zpool prefetch -t ddt $TESTPOOL
log_must getddtstats reloaded $TESTPOOL
log_note "reloaded stats: ${reloaded}"
log_must test "${reloaded.ondisk}" -ge "1048576"
log_must test "${reloaded.incore}" -ge "1048576"
log_must test "${reloaded.cached}" -eq "${reloaded.incore}"

log_pass "'zpool prefetch -t ddt <pool>' success."
