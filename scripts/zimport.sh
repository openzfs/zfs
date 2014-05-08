#!/bin/bash
#
# Verify that an assortment of known good reference pools can be imported
# using different versions of the ZoL code.
#
# By default references pools for the major ZFS implementation will be
# checked against the most recent ZoL tags and the master development branch.
# Alternate tags or branches may be verified with the '-s <src-tag> option.
# Passing the keyword "installed" will instruct the script to test whatever
# version is installed.
#
# Preferentially a reference pool is used for all tests.  However, if one
# does not exist and the pool-tag matches one of the src-tags then a new
# reference pool will be created using binaries from that source build.
# This is particularly useful when you need to test your changes before
# opening a pull request.  The keyword 'all' can be used as short hand
# refer to all available reference pools.
#
# New reference pools may be added by placing a bzip2 compressed tarball
# of the pool in the scripts/zfs-images directory and then passing
# the -p <pool-tag> option.  To increase the test coverage reference pools
# should be collected for all the major ZFS implementations.  Having these
# pools easily available is also helpful to the developers.
#
# Care should be taken to run these tests with a kernel supported by all
# the listed tags.  Otherwise build failure will cause false positives.
#
#
# EXAMPLES:
#
# The following example will verify the zfs-0.6.2 tag, the master branch,
# and the installed zfs version can correctly import the listed pools.
# Note there is no reference pool available for master and installed but
# because binaries are available one is automatically constructed.  The
# working directory is also preserved between runs (-k) preventing the
# need to rebuild from source for multiple runs.
#
#  zimport.sh -k -f /var/tmp/zimport \
#      -s "zfs-0.6.2 master installed" \
#      -p "zevo-1.1.1 zol-0.6.2 zol-0.6.2-173 master installed"
#
# --------------------- ZFS on Linux Source Versions --------------
#                 zfs-0.6.2       master          0.6.2-175_g36eb554
# -----------------------------------------------------------------
# Clone SPL       Local		Local		Skip
# Clone ZFS       Local		Local		Skip
# Build SPL       Pass		Pass		Skip
# Build ZFS       Pass		Pass		Skip
# -----------------------------------------------------------------
# zevo-1.1.1      Pass		Pass		Pass
# zol-0.6.2       Pass		Pass		Pass
# zol-0.6.2-173   Fail		Pass		Pass
# master          Pass		Pass		Pass
# installed       Pass		Pass		Pass
#
basedir="$(dirname $0)"

SCRIPT_COMMON=common.sh
if [ -f "${basedir}/${SCRIPT_COMMON}" ]; then
. "${basedir}/${SCRIPT_COMMON}"
else
echo "Missing helper script ${SCRIPT_COMMON}" && exit 1
fi

PROG=zimport.sh

SRC_TAGS="zfs-0.6.1 zfs-0.6.2 master"
POOL_TAGS="all master"
TEST_DIR=`mktemp -u -d -p /var/tmp zimport.XXXXXXXX`
KEEP=0
VERBOSE=0
COLOR=1
REPO="https://github.com/zfsonlinux"
IMAGES_DIR="$SCRIPTDIR/zfs-images/"
IMAGES_TAR="https://github.com/zfsonlinux/zfs-images/tarball/master"
CPUS=`grep -c ^processor /proc/cpuinfo`
ERROR=0

usage() {
cat << EOF
USAGE:
zimport.sh [hvl] [-r repo] [-s src-tag] [-i pool-dir] [-p pool-tag] [-f path]

DESCRIPTION:
	ZPOOL import verification tests

OPTIONS:
	-h                Show this message
	-v                Verbose
	-c                No color
	-k                Keep temporary directory
	-r <repo>         Source repository ($REPO)
	-s <src-tag>...   Verify ZoL versions with the listed tags
	-i <pool-dir>     Pool image directory
	-p <pool-tag>...  Verify pools created with the listed tags
	-f <path>         Temporary directory to use

EOF
}

while getopts 'hvckr:s:i:p:f:?' OPTION; do
	case $OPTION in
	h)
		usage
		exit 1
		;;
	v)
		VERBOSE=1
		;;
	c)
		COLOR=0
		;;
	k)
		KEEP=1
		;;
	r)
		REPO="$OPTARG"
		;;
	s)
		SRC_TAGS="$OPTARG"
		;;
	i)
		IMAGES_DIR="$OPTARG"
		;;
	p)
		POOL_TAGS="$OPTARG"
		;;
	f)
		TEST_DIR="$OPTARG"
		;;
	?)
		usage
		exit
		;;
	esac
done

# Initialize the test suite
init
check_modules || die "ZFS modules must be unloaded"

SRC_DIR="$TEST_DIR/src"
SRC_DIR_SPL="$SRC_DIR/spl"
SRC_DIR_ZFS="$SRC_DIR/zfs"

if [ $COLOR -eq 0 ]; then
	COLOR_GREEN=""
	COLOR_BROWN=""
	COLOR_RED=""
	COLOR_RESET=""
fi

pass_nonewline() {
	echo -n -e "${COLOR_GREEN}Pass${COLOR_RESET}\t\t"
}

skip_nonewline() {
	echo -n -e "${COLOR_BROWN}Skip${COLOR_RESET}\t\t"
}

fail_nonewline() {
	echo -n -e "${COLOR_RED}Fail${COLOR_RESET}\t\t"
}

#
# Set several helper variables which are derived from a source tag.
#
# SPL_TAG - The tag zfs-x.y.z is translated to spl-x.y.z.
# SPL_DIR - The spl directory name.
# SPL_URL - The spl github URL to fetch the tarball.
# ZFS_TAG - The passed zfs-x.y.z tag
# ZFS_DIR - The zfs directory name
# ZFS_URL - The zfs github URL to fetch the tarball
#
src_set_vars() {
	local TAG=$1

	SPL_TAG=`echo $TAG | sed -e 's/zfs/spl/'`
	SPL_DIR=$SRC_DIR_SPL/$SPL_TAG
	SPL_URL=$REPO/spl/tarball/$SPL_TAG

	ZFS_TAG=$TAG
	ZFS_DIR=$SRC_DIR_ZFS/$ZFS_TAG
	ZFS_URL=$REPO/zfs/tarball/$ZFS_TAG

	if [ "$TAG" = "installed" ]; then
		ZPOOL_CMD=`which zpool`
		ZFS_CMD=`which zfs`
		ZFS_SH="/usr/share/zfs/zfs.sh"
		ZPOOL_CREATE="/usr/share/zfs/zpool-create.sh"
	else
		ZPOOL_CMD="./cmd/zpool/zpool"
		ZFS_CMD="./cmd/zfs/zfs"
		ZFS_SH="./scripts/zfs.sh"
		ZPOOL_CREATE="./scripts/zpool-create.sh"
	fi
}

#
# Set several helper variables which are derived from a pool name such
# as zol-0.6.x, zevo-1.1.1, etc.  These refer to example pools from various
# ZFS implementations which are used to verify compatibility.
#
# POOL_TAG          - The example pools name in scripts/zfs-images/.
# POOL_BZIP         - The full path to the example bzip2 compressed pool.
# POOL_DIR          - The top level test path for this pool.
# POOL_DIR_PRISTINE - The directory containing a pristine version of the pool.
# POOL_DIR_COPY     - The directory containing a working copy of the pool.
# POOL_DIR_SRC      - Location of a source build if it exists for this pool.
#
pool_set_vars() {
	local TAG=$1

	POOL_TAG=$TAG
	POOL_BZIP=$IMAGES_DIR/$POOL_TAG.tar.bz2
	POOL_DIR=$TEST_DIR/pools/$POOL_TAG
	POOL_DIR_PRISTINE=$POOL_DIR/pristine
	POOL_DIR_COPY=$POOL_DIR/copy
	POOL_DIR_SRC=`echo -n "$SRC_DIR_ZFS/"; \
	    echo "$POOL_TAG" | sed -e 's/zol/zfs/'`
}

#
# Construct a non-trivial pool given a specific version of the source.  More
# interesting pools provide better test coverage so this function should
# extended as needed to create more realistic pools.
#
pool_create() {
	pool_set_vars $1
	src_set_vars $1

	if [ "$POOL_TAG" != "installed" ]; then
		cd $POOL_DIR_SRC
	fi

	$ZFS_SH zfs="spa_config_path=$POOL_DIR_PRISTINE" || fail 1

	# Create a file vdev RAIDZ pool.
	FILEDIR="$POOL_DIR_PRISTINE" $ZPOOL_CREATE \
	    -c file-raidz -p $POOL_TAG -v >/dev/null || fail 2

	# Create a pool/fs filesystem with some random contents.
	$ZFS_CMD create $POOL_TAG/fs || fail 3
	populate /$POOL_TAG/fs/ 10 100

	# Snapshot that filesystem, clone it, remove the files/dirs,
	# replace them with new files/dirs.
	$ZFS_CMD snap $POOL_TAG/fs@snap || fail 4
	$ZFS_CMD clone $POOL_TAG/fs@snap $POOL_TAG/clone || fail 5
	rm -Rf /$POOL_TAG/clone/* || fail 6
	populate /$POOL_TAG/clone/ 10 100

	# Scrub the pool, delay slightly, then export it.  It is now
	# somewhat interesting for testing purposes.
	$ZPOOL_CMD scrub $POOL_TAG || fail 7
	sleep 10
	$ZPOOL_CMD export $POOL_TAG || fail 8

	$ZFS_SH -u || fail 9
}

# If the zfs-images directory doesn't exist fetch a copy from Github then
# cache it in the $TEST_DIR and update $IMAGES_DIR.
if [ ! -d $IMAGES_DIR ]; then
	IMAGES_DIR="$TEST_DIR/zfs-images"
	mkdir -p $IMAGES_DIR
	curl -sL $IMAGES_TAR | \
	    tar -xz -C $IMAGES_DIR --strip-components=1 || fail 10
fi

# Given the available images in the zfs-images directory substitute the
# list of available images for the reserved keywork 'all'.
for TAG in $POOL_TAGS; do

	if  [ "$TAG" = "all" ]; then
		ALL_TAGS=`ls $IMAGES_DIR | grep "tar.bz2" | \
		    sed 's/.tar.bz2//' | tr '\n' ' '`
		NEW_TAGS="$NEW_TAGS $ALL_TAGS"
	else
		NEW_TAGS="$NEW_TAGS $TAG"
	fi
done
POOL_TAGS="$NEW_TAGS"

if [ $VERBOSE -ne 0 ]; then
	echo "---------------------------- Options ----------------------------"
	echo "VERBOSE=$VERBOSE"
	echo "KEEP=$KEEP"
	echo "REPO=$REPO"
	echo "SRC_TAGS="$SRC_TAGS""
	echo "POOL_TAGS="$POOL_TAGS""
	echo "PATH=$TEST_DIR"
	echo
fi

if [ ! -d $TEST_DIR ]; then
	mkdir -p $TEST_DIR
fi

if [ ! -d $SRC_DIR ]; then
	mkdir -p $SRC_DIR
fi

# Print a header for all tags which are being tested.
echo "--------------------- ZFS on Linux Source Versions --------------"
printf "%-16s" " "
for TAG in $SRC_TAGS; do
	src_set_vars $TAG

	if [ "$TAG" = "installed" ]; then
		ZFS_VERSION=`modinfo zfs | awk '/version:/ { print $2; exit }'`
		if [ -n "$ZFS_VERSION" ]; then
			printf "%-16s" $ZFS_VERSION
		else
			echo "ZFS is not installed\n"
			fail
		fi
	else
		printf "%-16s" $TAG
	fi
done
echo -e "\n-----------------------------------------------------------------"

#
# Attempt to generate the tarball from your local git repository, if that
# fails then attempt to download the tarball from Github.
#
printf "%-16s" "Clone SPL"
for TAG in $SRC_TAGS; do
	src_set_vars $TAG

	if [ -d $SPL_DIR ]; then
		skip_nonewline
	elif  [ "$SPL_TAG" = "installed" ]; then
		skip_nonewline
	else
		cd $SRC_DIR

		if [ ! -d $SRC_DIR_SPL ]; then
			mkdir -p $SRC_DIR_SPL
		fi

		git archive --format=tar --prefix=$SPL_TAG/ $SPL_TAG \
		    -o $SRC_DIR_SPL/$SPL_TAG.tar &>/dev/nul || \
		    rm $SRC_DIR_SPL/$SPL_TAG.tar
		if [ -s $SRC_DIR_SPL/$SPL_TAG.tar ]; then
			tar -xf $SRC_DIR_SPL/$SPL_TAG.tar -C $SRC_DIR_SPL
			rm $SRC_DIR_SPL/$SPL_TAG.tar
			echo -n -e "${COLOR_GREEN}Local${COLOR_RESET}\t\t"
		else
			mkdir -p $SPL_DIR || fail 1
			curl -sL $SPL_URL | tar -xz -C $SPL_DIR \
			    --strip-components=1 || fail 2
			echo -n -e "${COLOR_GREEN}Remote${COLOR_RESET}\t\t"
		fi
	fi
done
printf "\n"

#
# Attempt to generate the tarball from your local git repository, if that
# fails then attempt to download the tarball from Github.
#
printf "%-16s" "Clone ZFS"
for TAG in $SRC_TAGS; do
	src_set_vars $TAG

	if [ -d $ZFS_DIR ]; then
		skip_nonewline
	elif  [ "$ZFS_TAG" = "installed" ]; then
		skip_nonewline
	else
		cd $SRC_DIR

		if [ ! -d $SRC_DIR_ZFS ]; then
			mkdir -p $SRC_DIR_ZFS
		fi

		git archive --format=tar --prefix=$ZFS_TAG/ $ZFS_TAG \
		    -o $SRC_DIR_ZFS/$ZFS_TAG.tar &>/dev/nul || \
		    rm $SRC_DIR_ZFS/$ZFS_TAG.tar
		if [ -s $SRC_DIR_ZFS/$ZFS_TAG.tar ]; then
			tar -xf $SRC_DIR_ZFS/$ZFS_TAG.tar -C $SRC_DIR_ZFS
			rm $SRC_DIR_ZFS/$ZFS_TAG.tar
			echo -n -e "${COLOR_GREEN}Local${COLOR_RESET}\t\t"
		else
			mkdir -p $ZFS_DIR || fail 1
			curl -sL $ZFS_URL | tar -xz -C $ZFS_DIR \
			    --strip-components=1 || fail 2
			echo -n -e "${COLOR_GREEN}Remote${COLOR_RESET}\t\t"
		fi
	fi
done
printf "\n"

# Build the listed tags
printf "%-16s" "Build SPL"
for TAG in $SRC_TAGS; do
	src_set_vars $TAG

	if [ -f $SPL_DIR/module/spl/spl.ko ]; then
		skip_nonewline
	elif  [ "$SPL_TAG" = "installed" ]; then
		skip_nonewline
	else
		cd $SPL_DIR
		make distclean &>/dev/null
		sh ./autogen.sh &>/dev/null || fail 1
		./configure &>/dev/null || fail 2
		make -s -j$CPUS &>/dev/null || fail 3
		pass_nonewline
	fi
done
printf "\n"

# Build the listed tags
printf "%-16s" "Build ZFS"
for TAG in $SRC_TAGS; do
	src_set_vars $TAG

	if [ -f $ZFS_DIR/module/zfs/zfs.ko ]; then
		skip_nonewline
	elif  [ "$ZFS_TAG" = "installed" ]; then
		skip_nonewline
	else
		cd $ZFS_DIR
		make distclean &>/dev/null
		sh ./autogen.sh &>/dev/null || fail 1
		./configure --with-spl=$SPL_DIR &>/dev/null || fail 2
		make -s -j$CPUS &>/dev/null || fail 3
		pass_nonewline
	fi
done
printf "\n"
echo "-----------------------------------------------------------------"

# Either create a new pool using 'zpool create', or alternately restore an
# existing pool from another ZFS implementation for compatibility testing.
for TAG in $POOL_TAGS; do
	pool_set_vars $TAG
	SKIP=0

	printf "%-16s" $POOL_TAG
	rm -Rf $POOL_DIR
	mkdir -p $POOL_DIR_PRISTINE

	# Use the existing compressed image if available.
	if [ -f $POOL_BZIP ]; then
		tar -xjf $POOL_BZIP -C $POOL_DIR_PRISTINE \
		    --strip-components=1 || fail 1
	# Use the installed version to create the pool.
	elif  [ "$TAG" = "installed" ]; then
		pool_create $TAG
	# A source build is available to create the pool.
	elif [ -d $POOL_DIR_SRC ]; then
		pool_create $TAG
	else
		SKIP=1
	fi

	# Verify 'zpool import' works for all listed source versions.
	for TAG in $SRC_TAGS; do

		if [ $SKIP -eq 1 ]; then
			skip_nonewline
			continue
		fi

		src_set_vars $TAG
		if [ "$TAG" != "installed" ]; then
			cd $ZFS_DIR
		fi
		$ZFS_SH zfs="spa_config_path=$POOL_DIR_COPY"

		cp -a --sparse=always $POOL_DIR_PRISTINE \
		    $POOL_DIR_COPY || fail 2
		POOL_NAME=`$ZPOOL_CMD import -d $POOL_DIR_COPY | \
		    awk '/pool:/ { print $2; exit 0 }'`

		$ZPOOL_CMD import -N -d $POOL_DIR_COPY $POOL_NAME &>/dev/null
		if [ $? -ne 0 ]; then
			fail_nonewline
			ERROR=1
		else
			$ZPOOL_CMD export $POOL_NAME || fail 3
			pass_nonewline
		fi

		rm -Rf $POOL_DIR_COPY

		$ZFS_SH -u || fail 4
	done
	printf "\n"
done

if [ ! $KEEP ]; then
	rm -Rf $TEST_DIR
fi

exit $ERROR
