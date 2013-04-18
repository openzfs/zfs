#!/bin/bash

BASETANK="share"
DATE=`date "+%Y%m%d"`

TEST_SMBFS=0
TEST_DESTROY=0

if [ -z "$1" ]; then
	echo "Usage: `basename $0` [unpack]<[smbfs][snapshot][all]>"
	exit 1
fi

set_onoff() {
	type="$1"
	dataset="$2"
	toggle="$3"

	current=`zfs get -H $type -o value $dataset`
	if [ "$current" != "$toggle" ]; then
		run "zfs set $type=$toggle $dataset"
	fi
}

check_exists() {
	dataset="$1"

	extra=""
	[ -n "$2" ] && extra="$2"

	zfs get all "$dataset" > /dev/null 2>&1
	if [ $? != 0 ]; then
		run "zfs create $extra $dataset"
	fi
}

check_shares() {
	if [ "$TEST_SMBFS" == "1" ]; then
		echo "Shares:"
		echo "=> usershare list:"
		net usershare list
		echo
		echo "=> /etc/dfs/sharetab:"
		cat /etc/dfs/sharetab
		echo
	fi

	sleep 2
}

test_header() {
	echo "TEST: $*"
	echo "======================================"
}

run() {
	cmd="$*"

	echo "CMD: $cmd"
	$cmd
}

# ---------
# Needs more work...
if echo "$*" | grep -qi "unpack"; then
	zfs unmount -a
	zfs unshare -a
	run "zfs destroy -r $BASETANK/tests"

	sh /etc/init.d/zfs stop

#	for tid in `grep ^tid /proc/net/iet/volume | sed "s@.*:\([0-9].*\) name.*@\1@"`
#	do
#		ietadm --op delete --tid $tid
#	done

	set -e
	rmmod `lsmod | grep ^z | grep -v zlib_deflate | sed 's@ .*@@'` spl zlib_deflate

	pushd / > /dev/null
	[ -f "tmp/zfs.tgz" ] && tar xzf tmp/zfs.tgz && rm tmp/zfs.tgz
	[ -f "tmp/spl.tgz" ] && tar xzf tmp/spl.tgz && rm tmp/spl.tgz
	popd > /dev/null

	depmod -a

	sh /etc/init.d/zfs start
	set +e
fi

# ---------
if echo "$*" | egrep -qi "smbfs|all"; then
	check_exists $BASETANK/tests

	TEST_SMBFS=1

	test_header "Exists || Create"
	str=
	for volnr in 1 2 3; do
		check_exists $BASETANK/tests/smbfs$volnr

		str="$str $BASETANK/tests/smbfs$volnr"
	done
	run "zfs get sharesmb $str"

	# Set sharesmb=on
	test_header "Enable SMB share"
	for volnr in 1 2 3; do
	    set_onoff sharesmb "$BASETANK/tests/smbfs$volnr" on
	    check_shares
	done

	# Share all
	test_header "Share all (individually)"
	for volnr in 1 2 3; do
	    run "zfs share $BASETANK/tests/smbfs$volnr"
	    check_shares
	done

	# Unshare all
	test_header "Unshare all (individually)"
	for volnr in 1 2 3; do
	    run "zfs unshare $BASETANK/tests/smbfs$volnr"
	    check_shares
	done

	# Change mountpoint - first unshare and then share individual
	test_header "Change mount point (unshare ; share)"
	mkdir -p /tests
	set_onoff sharesmb "$str" off
	for volnr in 3 1 2; do
		run "zfs set mountpoint=/tests/smbfs$volnr $BASETANK/tests/smbfs$volnr"
		echo "CMD: mount | grep ^$BASETANK/tests/smbfs$volnr"
		mount | grep ^$BASETANK/tests/smbfs$volnr
		echo

		run "zfs mount $BASETANK/tests/smbfs$volnr"
		echo "CMD: mount | grep ^$BASETANK/tests/smbfs$volnr"
		mount | grep ^$BASETANK/tests/smbfs$volnr
		echo

		set_onoff sharesmb "$BASETANK/tests/smbfs$volnr" on
		check_shares

		run "zfs share $BASETANK/tests/smbfs$volnr"
		check_shares

		echo "-------------------"
	done

	# Change mountpoint - remounting
	test_header "Change mount point (remounting)"
	for volnr in 3 1 2; do
		run "zfs set mountpoint=/$BASETANK/tests/smbfs$volnr $BASETANK/tests/smbfs$volnr"
		echo "CMD: mount | grep ^$BASETANK/tests/smbfs$volnr"
		mount | grep ^$BASETANK/tests/smbfs$volnr
		echo
		# => Doesn't seem to remount (!?)

		run "zfs mount $BASETANK/tests/smbfs$volnr"
		echo "CMD: mount | grep ^$BASETANK/tests/smbfs$volnr"
		mount | grep ^$BASETANK/tests/smbfs$volnr
		echo
		# => Doesn't seem to reshare (!?)

		check_shares

		run "zfs share $BASETANK/tests/smbfs$volnr"
		check_shares

		echo "-------------------"
	done
fi

# ---------
if echo "$*" | egrep -qi "smbfs|all"; then
	test_header "Unshare + Share all"

	run "zfs share -a" ; check_shares
	run "zfs unshare -a" ; check_shares
fi

# ---------
if echo "$*" | grep -qi "snapshot|all"; then
	test_header "Snapshots"

	echo ; echo "-------------------"
	check_exists $BASETANK/tests/destroy
	check_exists $BASETANK/tests/destroy/destroy1
	run "zfs destroy -r $BASETANK/tests/destroy"

	echo ; echo "-------------------"
	check_exists $BASETANK/tests/destroy
	run "zfs snapshot $BASETANK/tests/destroy@$DATE"
	run "zfs destroy -r $BASETANK/tests/destroy"

	echo ; echo "-------------------"
	check_exists $BASETANK/tests/destroy
	run "zfs snapshot $BASETANK/tests/destroy@$DATE"
	run "zfs destroy -r $BASETANK/tests/destroy@$DATE"
	run "zfs destroy -r $BASETANK/tests/destroy"
fi

if echo "$*" | egrep -qi "smbfs|snapshot|all"; then
	test_header "Cleanup (Share all + Destroy all)"

	run "zfs share -a"
	check_shares

	run "zfs destroy -r $BASETANK/tests"
	check_shares

	run "zfs list"
fi
