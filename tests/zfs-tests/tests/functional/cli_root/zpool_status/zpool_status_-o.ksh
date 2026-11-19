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
# Verify 'zpool status -o' shows the pool properties 'zpool get' reports on
# the row of the pool and the vdev properties on the rows of the vdevs, in
# columns and in the JSON output.
#
# STRATEGY:
# 1. Create a pool with a mirror of files, a log on a disk, a cache device
#    and a spare.  Set a comment and a user property on the pool and,
#    with other values, on one of the files.
# 2. Ask for a pool-only property (dedupratio), a vdev-only one (rotational),
#    names of both kinds (guid, size, comment) and the user property.
# 3. Verify the row of the pool ends with the pool's values and "-" for
#    rotational, and every vdev row, the spare's included, with the vdev's
#    values and "-" for dedupratio, as 'zpool get' reports them, under their
#    names in lower case, and with exact values with -p.
# 4. Verify the "properties" object of the pool object of 'zpool status -j -o'
#    and that of every vdev object, the root vdev's included, are what
#    'zpool get -j' reports for the pool properties and the vdev properties,
#    also with --json-flat-vdevs and --json-int, and, without --json-int,
#    that every guid in them is the guid of the object.
# 5. Verify that two -o options show the columns of both, in order.
# 6. Verify an unknown property, 'all' and an empty list are refused.
#

verify_runnable "global"

DISK2="$(echo $DISKS | cut -d' ' -f2)"
VDEV1=$TEST_BASE_DIR/status-o-vdev1
VDEV2=$TEST_BASE_DIR/status-o-vdev2
L2C=$TEST_BASE_DIR/status-o-l2c
SPARE=$TEST_BASE_DIR/status-o-spare
TMP=$TEST_BASE_DIR/status-o-tmp
UPROP=org.openzfs:status-o
PROPS=guid,size,comment,dedupratio,rotational,$UPROP
POOL_PROPS=guid,size,comment,dedupratio,$UPROP
VDEV_PROPS=guid,size,comment,rotational,$UPROP

function cleanup
{
	destroy_pool $TESTPOOL2
	rm -f $VDEV1 $VDEV2 $L2C $SPARE $TMP.exp $TMP.out $TMP.status \
	    $TMP.rows $TMP.json
}

# Print the guid and the name of every vdev of the pool, the root vdev's
# included, one vdev per line.
function vdevs
{
	zpool status -j $TESTPOOL2 | jq -r '.. | objects |
	    select(has("vdev_type")) | "\(.guid) \(.name)"' | sort -u
}

# Print, one line per row of 'zpool status -o $PROPS', the values 'zpool get'
# reports: the pool properties of the pool, then the vdev properties of each
# vdev, with "-" where a name is not a property of that kind.  Pass -p for
# exact values.
function get_values
{
	typeset p=$1 guid name

	set -- $(zpool get -H $p -o value $POOL_PROPS $TESTPOOL2)
	echo $1 $2 $3 $4 - $5
	vdevs | grep -v " $TESTPOOL2\$" | while read guid name; do
		set -- $(zpool get -H $p -o value $VDEV_PROPS $TESTPOOL2 $guid)
		echo $1 $2 $3 - $4 $5
	done
}

# Print, sorted, the values ending each row of 'zpool status -o $PROPS', run
# with the given options.  Fail if the headers are not the expected ones, or if
# a row is not as long as the header line, which is the case when every
# column, the spare's included, lines up with its header.
function status_values
{
	zpool status "$@" -o $PROPS $TESTPOOL2 > $TMP.status || return 1
	awk -v hdr="guid size comment dedupratio rotational $UPROP" '
	    /^\tNAME / {
		h = $(NF - 5)
		for (i = 4; i >= 0; i--)
			h = h " " $(NF - i)
		if (h != hdr)
			exit 1
		len = length($0)
		conf = 1
		next
	    }
	    conf && /^$/ { exit 0 }
	    conf && NF >= 8 {
		if (length($0) != len)
			exit 1
		print $(NF - 5), $(NF - 4), $(NF - 3), $(NF - 2), $(NF - 1), $NF
	    }' $TMP.status > $TMP.rows || return 1
	sort $TMP.rows
}

# Check the "properties" objects of 'zpool status -j -o $PROPS', run with the
# given options, against 'zpool get -j': the pool properties for the pool
# object, the vdev properties for each vdev object.  Without --json-int, the
# guids in them are also compared to the guids of the objects.  With
# --json-int, a property value and the guid of an object are printed by
# different code, and the comparison with 'zpool get -j --json-int', which
# prints the property values with the same code, is the one made.
function check_json
{
	typeset int="" guid name

	[[ " $* " == *" --json-int "* ]] && int="--json-int"
	log_must eval "zpool status -j $* -o $PROPS $TESTPOOL2 > $TMP.json"
	if [[ -z "$int" ]]; then
		log_must jq -e --arg p $TESTPOOL2 '.pools[$p] |
		    .properties.guid.value == .pool_guid' $TMP.json
		log_must jq -e 'all(.. | objects | select(has("vdev_type"));
		    .properties.guid.value == .guid)' $TMP.json
	fi

	zpool get -j $int $POOL_PROPS $TESTPOOL2 | \
	    jq -S '.pools[].properties' > $TMP.exp
	jq -S --arg p $TESTPOOL2 '.pools[$p].properties' $TMP.json > $TMP.out
	log_note "zpool status -j $* -o, pool:" "$(jq -c . $TMP.out)"
	log_must diff $TMP.exp $TMP.out

	vdevs | while read guid name; do
		zpool get -j $int $VDEV_PROPS $TESTPOOL2 $guid | \
		    jq -S '.vdevs[].properties' > $TMP.exp
		jq -S --arg n "$name" '[.. | objects | select(has("vdev_type"))
		    | select(.name == $n)][0].properties' $TMP.json > $TMP.out
		log_note "zpool status -j $* -o, $name:" "$(jq -c . $TMP.out)"
		log_must diff $TMP.exp $TMP.out
	done
}

log_assert "'zpool status -o' shows the pool and vdev properties" \
    "'zpool get' reports"
log_onexit cleanup

log_must truncate -s $MINVDEVSIZE $VDEV1 $VDEV2 $L2C $SPARE
log_must zpool create -f -o ashift=12 $TESTPOOL2 mirror $VDEV1 $VDEV2 \
    log $DISK2 cache $L2C spare $SPARE
log_must zpool set comment=pool-note $TESTPOOL2
log_must zpool set $UPROP=shelf-a $TESTPOOL2
log_must zpool set comment=vdev-note $TESTPOOL2 $VDEV1
log_must zpool set $UPROP=bay7 $TESTPOOL2 $VDEV1

typeset -i nvdevs=$(vdevs | wc -l)
(( nvdevs == 7 )) || log_fail "expected 7 vdevs with the root, found $nvdevs"

for opts in "" "-p"; do
	get_values $opts | sort > $TMP.exp
	log_note "zpool get $opts:" "$(cat $TMP.exp)"
	status_values $opts > $TMP.out || \
	    log_fail "zpool status $opts -o: bad header or alignment"
	log_note "zpool status $opts -o:" "$(cat $TMP.out)"
	log_must diff $TMP.exp $TMP.out
done

for opts in "" "--json-flat-vdevs" "--json-int"; do
	check_json $opts
done

log_must eval "zpool status -o $PROPS $TESTPOOL2 > $TMP.exp"
log_must eval "zpool status -o guid,size,comment -o dedupratio" \
    "-o rotational,$UPROP $TESTPOOL2 > $TMP.out"
log_must diff $TMP.exp $TMP.out

log_mustnot zpool status -o nosuchprop $TESTPOOL2
log_mustnot zpool status -j -o rotational,nosuchprop $TESTPOOL2
log_mustnot zpool status -o all $TESTPOOL2
log_mustnot zpool status -o "" $TESTPOOL2

log_pass "'zpool status -o' shows the pool and vdev properties" \
    "'zpool get' reports"
