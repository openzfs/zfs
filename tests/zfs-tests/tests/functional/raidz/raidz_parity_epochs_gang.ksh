#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A gang header takes the sectors of the parity in force when its
#	block was born, so freeing or claiming it must use that parity,
#	not the one of txg 0.  This test forces gang blocks on a disposable
#	raidz pool before and after a parity-epoch boundary, in both
#	directions, and checks that nothing leaks or is over-released.
#
# STRATEGY:
#	1. Create a 5-wide raidz pool and write gang blocks with the
#	   metaslab force-ganging tunables (born at the base parity).
#	2. Export it and store a {0:5:P, T:5:Q} table with zhack, T the
#	   first txg it allows, above every existing txg.
#	3. Import it and write more gang blocks (born at parity Q).
#	4. Read every file back and scrub; zdb finds no leak, which also
#	   checks the claims of the gang headers of both parities.
#	5. Remove half of the files, then all of them, export and check with
#	   zdb again: no leak, and no range tree panic from a header freed
#	   with the wrong size.
#	6. Run it for a promotion (raidz1 to parity 2) and a demotion (raidz2
#	   to parity 1).
#

verify_runnable "global"

typeset BASEDIR=$TEST_BASE_DIR/raidz_gang.$$
typeset -a VDEVS
typeset -i ndev=5
typeset -i nfiles=16
typeset POOL=${TESTPOOL}_gang

function cleanup
{
	poolexists $POOL && destroy_pool $POOL
	restore_tunable METASLAB_FORCE_GANGING
	restore_tunable METASLAB_FORCE_GANGING_PCT
	rm -rf $BASEDIR
}

# zdb(8) prints leaked space but still exits 0, so look for the verdict.
function check_no_leaks
{
	typeset out=$BASEDIR/zdb.out

	log_must eval "zdb -e -p $BASEDIR -bcc $POOL > $out 2>&1"
	log_must grep -q "No leaks" $out
	log_mustnot grep -q "leaked space" $out
}

function write_files # <first> <count>
{
	typeset -i i=$1
	typeset -i last

	(( last = $1 + $2 ))

	while (( i < last )); do
		log_must dd if=/dev/urandom of=/$POOL/data/f$i bs=128k count=1
		(( i = i + 1 ))
	done
	log_must sync_pool $POOL
}

function run_case # <base parity> <other parity>
{
	typeset -i base=$1
	typeset -i other=$2
	typeset -i i

	i=0
	while (( i < ndev )); do
		log_must truncate -s 512M ${VDEVS[$i]}
		(( i = i + 1 ))
	done

	log_must zpool create -f -o ashift=12 $POOL raidz$base ${VDEVS[@]}
	log_must zfs create -o recordsize=128k -o compression=off \
	    $POOL/data
	log_must set_tunable64 METASLAB_FORCE_GANGING 100000
	log_must set_tunable32 METASLAB_FORCE_GANGING_PCT 100

	# Gang blocks born at the base parity.
	write_files 0 $nfiles
	log_must zpool export $POOL

	# A boundary above every txg of the pool, zhack's own import included.
	log_must zhack -d $BASEDIR raidz_epochs $POOL 0 \
	    0:${ndev}:${base} next:${ndev}:${other}
	log_must zpool import -d $BASEDIR $POOL
	log_must eval "zpool get -H -o value feature@raidz_parity_epochs \
	    $POOL | grep -q active"

	# Gang blocks born at the other parity.
	write_files $nfiles $nfiles
	log_must verify_pool $POOL
	log_must eval "cat /$POOL/data/f* > /dev/null"
	log_must zpool export $POOL
	check_no_leaks

	# Free the blocks of both parities, half and then all of them.
	log_must zpool import -d $BASEDIR $POOL
	i=0
	while (( i < 2 * nfiles )); do
		log_must rm /$POOL/data/f$i
		(( i = i + 2 ))
	done
	log_must sync_pool $POOL
	log_must sync_pool $POOL
	log_must sync_pool $POOL
	log_must eval "rm -f /$POOL/data/f*"
	log_must sync_pool $POOL
	log_must sync_pool $POOL
	log_must sync_pool $POOL
	log_must zpool export $POOL
	check_no_leaks

	log_must zpool import -d $BASEDIR $POOL
	log_must verify_pool $POOL
	log_must destroy_pool $POOL
	log_must rm -f ${VDEVS[@]}
}

log_onexit cleanup
log_assert "gang headers born after a parity change are freed and claimed whole"

save_tunable METASLAB_FORCE_GANGING
save_tunable METASLAB_FORCE_GANGING_PCT

log_must mkdir -p $BASEDIR
typeset -i n=0
while (( n < ndev )); do
	VDEVS[$n]=$BASEDIR/vdev$n
	(( n = n + 1 ))
done

run_case 1 2
run_case 2 1

log_pass "gang headers born after a parity change are freed and claimed whole"
