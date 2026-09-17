#!/usr/bin/env bash
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

SCRIPT=$(basename "$0")

function usage {
	echo \
"USAGE:
	$SCRIPT <ZTS_ARTIFACTS_TARBALL>

EXAMPLE:
	$SCRIPT qemu-fedora43.tar.bz2

        Where 'qemu-fedora43.tar.bz2' was the ZTS artifacts file from a test
        run (like https://github.com/openzfs/zfs/actions/runs/33776576500/artifacts/9909970630)

Read a github ZTS artifact file and write out an associative array containing
all the test group run times (in seconds).  The array should be copy-n-pasted
into scripts/zfs-tests.sh, and is used to balance the test groups distributed to
the VMs when the CI is run.  It is expected this script will be run periodically
to update testdb[] in zfs-tests.sh."
	exit
}

if [ "$#" -ne 1 ]; then
	usage
fi

TARBALL="$1"
TMPDIR="$(mktemp -d -t ${SCRIPT}_XXXX)"

# Extract only the vm1log.txt and vm2log.txt files.
tar -xf "$TARBALL" -C "$TMPDIR" '*/vm*log.txt'

TMP="$(mktemp -t ${SCRIPT}_XXXX)"

# Go though all the vm*log.txt files we extracted. There should only be two.
# Write test times to TMP, like:
#
# functional/atime/atime_003_pos [00:10]
# functional/atime/root_atime_off [00:06]
# functional/atime/root_atime_on [00:10]
# functional/atime/root_relatime_on [00:10]
# functional/atime/cleanup [00:00]
# functional/block_cloning/setup [00:00]
# functional/block_cloning/block_cloning_clone_mmap_cached [00:10]
# functional/block_cloning/block_cloning_copyfilerange [00:02]
# ...
for i in $(find $TMPDIR -type f) ; do
	sed 's/(Linux): //g; s/(FreeBSD): //g' $i | \
	    awk '/Results/{exit}; /\[PASS\]/{print $3" "$(NF-1)}' | \
	    sed 's;/usr/share/zfs/zfs-tests/tests/;;g' | \
	    sed 's;/usr/local/share/zfs/zfs-tests/;;g' >> $TMP
done
rm -fr "$TMPDIR"

# Lines should naturally be grouped by name by virtue of the vm*log.txt
# format, but sort them just in case.
sort -o $TMP $TMP

# Go though each group, sum up the total time of all their tests, and output
# the total number of seconds per test group, like:
#
# zpool_import 1734
# mmp 1470
# events 888
# rsend 775
# zpool_scrub 757
# slog 706
# direct 634
# pool_checkpoint 492
# zpool_create 413
# zpool_upgrade 309
# ...
#
TMP2=$(mktemp -t ${SCRIPT}_XXXX)

cat $TMP | awk -F '[/ \\[:\\]]' \
'BEGIN{name=""; sum=0}
{
	thisname=$(NF-5);
	if (name!=thisname) {
		if (name!="") {print name" "sum};
		name=thisname;
		sum=0
	};
	sum+=$(NF-2)* 60 + $(NF-1)};
	END {print name" "sum}' | sort -r -n -k 2 > $TMP2

rm "$TMP"

# Convert the list we just made into a bash associative array like:
#
# declare -A testdb=(["mmp"]=1457 ["redundancy"]=1436 ["events"]=914
# ["replacement"]=742 ["raidz"]=738 ["rsend"]=683 ["l2arc"]=588
# ["pool_checkpoint"]=581 ["fault"]=562 ["zpool_import"]=536 ["slog"]=532
# ...
#
# This output should be copy-n-pasted into zts-tests.sh
awk '
BEGIN {printf "declare -A testdb=("}
{printf "%s ", "[\""$1"\"]="$2}
END {print ")"}' $TMP2 | sed 's/ )/)/g' | fold -b -s -w 80 | sed -e 's/\ $//g'

rm "$TMP2"
