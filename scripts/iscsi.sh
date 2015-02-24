#!/bin/bash

if [ -d /sys/kernel/scst_tgt ]; then
    SYSFS=/sys/kernel/scst_tgt
elif [ -f /proc/net/iet/volume ]; then
    IETFS=/proc/net/iet/volume
elif type tgtadm > /dev/null 2>&1; then
    TGT=`which tgtadm`
elif type targetcli > /dev/null 2>&1; then
    LIO=`which targetcli`
fi

BASETANK=`zpool list -H | sed 's@	.*@@'`
DATE=`date "+%Y%m%d"`

if [ -z "$1" ]; then
	echo "Usage: `basename $0` [debug] [iscsi]"
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
	if [ -n "$SYSFS" ]; then
	    if type list_scst.pl > /dev/null 2>&1; then
		run "list_scst.pl"
	    else
		run "find $SYSFS/targets/iscsi/iqn.* -maxdepth 0"
	    fi
	elif [ -n "$IETFS" ]; then
	    run "cat /proc/net/iet/volume" | \
		    egrep '^CMD: |tid:'
	elif [ -n "$TGT" ]; then
	    run "tgtadm --lld iscsi --op show --mode target" | \
		    egrep '^CMD: |Target '
	elif [ -n "$LIO" ]; then
	    run "echo '/ ls' | targetcli"
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

# =================================================

# -------------------------------------------------
test_header "Basic setup"

check_exists $BASETANK/tests

str=
for volnr in {1..5}; do
	check_exists $BASETANK/tests/iscsi$volnr "-s -V 15G"

	str="$str $BASETANK/tests/iscsi$volnr"
done
run "zfs get shareiscsi $str"
check_shares

# -------------------------------------------------
echo ; test_header "Autoshare"
for volnr in {1..5}; do
	set_onoff shareiscsi $BASETANK/tests/iscsi$volnr on
	check_shares
done

# -------------------------------------------------
echo ; test_header "Unshare single"
for volnr in {1..5}; do
	run "zfs unshare $BASETANK/tests/iscsi$volnr"
	check_shares
done

# -------------------------------------------------
echo ; test_header "Share single"
for volnr in {1..5}; do
	# NOTE: This is expected to fail if vol already
	# 	existed (not created by script) and had
	#	shareiscsi=on set.
	run "zfs share $BASETANK/tests/iscsi$volnr"
	check_shares
done

# -------------------------------------------------
echo ; test_header "Autounshare"
for volnr in {1..5}; do
	set_onoff shareiscsi "$BASETANK/tests/iscsi$volnr" off
	check_shares
done

# -------------------------------------------------
# 1. Start out unshared ('Share single' above)
# 2. Rename ZVOL
# 3. Share
# 4. Rename ZVOL
echo ; test_header "Rename volume (shared ; unshared)"
for volnr in {1..5}; do
	run "zfs rename $BASETANK/tests/iscsi$volnr $BASETANK/tests/new.iscsi$volnr"
	check_shares

	set_onoff shareiscsi "$BASETANK/tests/new.iscsi$volnr" on
	check_shares

	run "zfs rename $BASETANK/tests/new.iscsi$volnr $BASETANK/tests/iscsi$volnr"
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
