#!/bin/bash

BASETANK=`zpool list -H | sed 's@	.*@@'`
DATE=`date "+%Y%m%d"`

if [ -z "$1" ]; then
	echo "Usage: `basename $0` [debug] [smbfs]"
	exit 1
fi
CMDLINE="$*"

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
	if type list_smbfs.sh > /dev/null 2>&1; then
	    list_smbfs.sh
	else
	    run "net conf listshares"
	fi
	run "cat /etc/dfs/sharetab"

	echo

	if ! echo "$CMDLINE" | egrep -qi "debug"; then
		sleep 2
	fi
}

test_header() {
	printf "TEST: %s\n" "$*"
	echo "======================================"
}

run() {
	cmd="$*"

	echo "CMD: $cmd"
	if ! echo "$CMDLINE" | egrep -qi "debug"; then
		$cmd 2>&1 | while IFS= read line; do
		    echo "     $line"
		done
	fi
}

check_mntpt() {
	volnr=$1

	echo "CMD: mount | grep ^$BASETANK/tests/smbfs$volnr"
	mount | grep ^$BASETANK/tests/smbfs$volnr | while IFS= read line; do
	    echo "     $line"
	done
}

# =================================================

# -------------------------------------------------
test_header "Basic setup"
check_exists $BASETANK/tests

str=
for volnr in 1 2 3; do
	check_exists $BASETANK/tests/smbfs$volnr

	str="$str $BASETANK/tests/smbfs$volnr"
done
run "zfs get sharesmb $str"
check_shares

# -------------------------------------------------
echo ; test_header "Autoshare"
for volnr in 1 2 3; do
	set_onoff sharesmb "$BASETANK/tests/smbfs$volnr" on
	check_shares
done

# -------------------------------------------------
echo ; test_header "Unshare single"
for volnr in 1 2 3; do
	run "zfs unshare $BASETANK/tests/smbfs$volnr"
	check_shares
done

# -------------------------------------------------
echo ; test_header "Share single"
for volnr in 1 2 3; do
	# NOTE: This is expected to fail if dataset already
	# 	existed (not created by script) and had
	#	sharesmb=on set.
	run "zfs share $BASETANK/tests/smbfs$volnr"
	check_shares
done

# -------------------------------------------------
echo ; test_header "Autounshare"
for volnr in 1 2 3; do
	set_onoff sharesmb "$BASETANK/tests/smbfs$volnr" off
	check_shares
done

# -------------------------------------------------
# 1. Start out unshared ('Autounshare' above)
# 2. Change the mountpoint
# 3. Share
# 4. Change mountpoint
echo ; test_header "Change mount point (unshared ; shared)"
mkdir -p /tests
for volnr in 3 1 2; do
	orig=`zfs get -H -o value mountpoint mypool/tests/smbfs$volnr`

	check_mntpt $volnr
	run "zfs set mountpoint=/tests/smbfs$volnr $BASETANK/tests/smbfs$volnr"
	check_mntpt $volnr
	echo

	set_onoff sharesmb "$BASETANK/tests/smbfs$volnr" on
	check_shares

	run "zfs set mountpoint=$orig $BASETANK/tests/smbfs$volnr"
	check_mntpt $volnr
	check_shares

	echo
done

# -------------------------------------------------
echo ; test_header "Unshare all"
run "zfs unshare -a"
check_shares

# -------------------------------------------------
echo ; test_header "Share all"
run "zfs share -a"
check_shares

# -------------------------------------------------
echo ; test_header "Destroy all"
run "zfs destroy -r $BASETANK/tests"
check_shares

# -------------------------------------------------
echo ; test_header "End"
run "zfs list"
check_shares
