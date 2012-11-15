#!/bin/bash

BASETANK="tank"
DATE=`date "+%Y%m%d"`

TEST_ISCSI=0
TEST_SMBFS=0
TEST_DESTROY=0

if [ -z "$1" ]; then
	echo "Usage: `basename $0` [unpack]<[iscsi][smbfs][snapshot][all]>"
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
	if [ "$TEST_ISCSI" == "1" -o  "$TEST_SMBFS" == "1" ]; then
		echo
		echo "Shares:"

		if [ "$TEST_ISCSI" == "1" ]; then
			cat /proc/net/iet/volume
			echo
		fi

		if [ "$TEST_SMBFS" == "1" ]; then
			net usershare list
			echo
		fi
	fi

	sleep 2
}

run() {
	cmd="$*"

	if [ "$TEST_ISCSI" == "1" -o  "$TEST_SMBFS" == "1" ]; then
		echo "-------------------"
	fi

	echo "CMD: $cmd"
	$cmd
}

check_exists $BASETANK/tests

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
		tar xzf tmp/zfs.tgz
	popd > /dev/null 

	depmod -a

	sh /etc/init.d/zfs start
	set +e
fi

# ---------
if echo "$*" | egrep -qi "iscsi|all"; then
	TEST_ISCSI=1

	for volnr in 1 2 3; do
		check_exists $BASETANK/tests/iscsi$volnr "-V 15G"
	done

	str=
	for volnr in 1 2 3; do
		str="$str $BASETANK/tests/iscsi$volnr"
	done
	run "zfs get shareiscsi $str"

	for volnr in 1 2 3; do
		set_onoff shareiscsi $BASETANK/tests/iscsi$volnr on
	done

	for volnr in 1 2 3; do
		run "zfs share $BASETANK/tests/iscsi$volnr" ; check_shares
	done

	for volnr in 2 1 3; do
		run "zfs unshare $BASETANK/tests/iscsi$volnr" ; check_shares
	done
fi

# ---------
if echo "$*" | egrep -qi "smbfs|all"; then
	TEST_SMBFS=1

	for volnr in 1 2 3; do
		check_exists $BASETANK/tests/smbfs$volnr
	done

	str=
	for volnr in 1 2 3; do
		str="$str $BASETANK/tests/iscsi$volnr"
	done
	run "zfs get sharesmb $str"

	for volnr in 1 2 3; do
		set_onoff sharesmb $BASETANK/tests/smbfs$volnr on
	done

	for volnr in 1 2 3; do
		run "zfs share $BASETANK/tests/smbfs$volnr" ; check_shares
	done

	for volnr in 3 1 2; do
		run "zfs unshare $BASETANK/tests/smbfs$volnr" ; check_shares
	done
fi

if echo "$*" | egrep -qi "iscsi|smbfs|all"; then
	run "zfs share -a" ; check_shares
	run "zfs unshare -a" ; check_shares

	if echo "$*" | egrep -qi "iscsi|all"; then
		for volnr in 1 2 3; do
			run "zfs destroy $BASETANK/tests/iscsi$volnr"
		done
	fi

	if echo "$*" | egrep -qi "smbfs|all"; then
		for volnr in 1 2 3; do
			run "zfs destroy $BASETANK/tests/smbfs$volnr"
		done
	fi
fi

# ---------
if echo "$*" | grep -qi "snapshot|all"; then
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

if echo "$*" | egrep -qi "iscsi|smbfs|all"; then
	run "zfs unshare -a"
	run "zfs destroy -r $BASETANK/tests"
fi
