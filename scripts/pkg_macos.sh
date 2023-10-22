#!/usr/bin/env bash

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
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

# Copyright (c) 2020 by ilovezfs
# Copyright (c) 2020 by Jorgen Lundman <lundman@lundman.net>

#
# A script to produce an installable .pkg for macOS.
#
# Environment variables:
#
#     $PKG_CODESIGN_KEY: Set to name of certificate for codesigning,
#        as named in Keychain. For example;
#        "Developer ID Application: Joergen  Lundman (735AM5QEU3)"
#
#     $PKG_NOTARIZE_KEY: Set to the notarize key you can create on
#        Apple developer pages. For example;
#        "awvz-fqoi-cxag-tymn"
#
#     $PKG_INSTALL_KEY: Set to the name of certificate for installer
#        signing, as named in Keychain, For example;
#        "Developer ID Installer: Joergen  Lundman (735AM5QEU3)"
#

BASE_DIR=$(dirname "$0")
SCRIPT_COMMON=common.sh
if [ -f "${BASE_DIR}/${SCRIPT_COMMON}" ]; then
    . "${BASE_DIR}/${SCRIPT_COMMON}"
else
    echo "Missing helper script ${SCRIPT_COMMON}" && exit 1
fi

WORKNAME="macos-root"
WORKDIR="/private/var/tmp/${WORKNAME}"

# If there are two dots "10.15.4", eat it
OS=$(sw_vers | awk '{if ($1 == "ProductVersion:") print $2;}')
OS=$(echo "$OS" | awk -F . '{if ($1 == 10) print $1"."$2; else print $1}')
RC=$(grep Release: META | awk '$2 ~ /rc/ { print $2}')

function usage
{
    echo "$0: Create installable pkg for macOS"
    echo ""
    echo " Options:"
    echo "   -l  skip make install step, using a folder of a previous run"
    echo "   -L  copy and fix external libraries"
    exit
}

while getopts "hlL" opt; do
    case $opt in
	l ) skip_install=1 ;;
	L ) fix_libraries=1 ;;
	h ) usage
	    exit 2
	    ;;
	* ) echo "Invalid argument: -$OPTARG";
	    usage
	    exit 1
    esac
done

case ${OS} in
    10.8|10.9|10.10|10.11|10.12|10.13)
	unset PKG_NOTARIZE_KEY
	echo "No notarize for OS $OS"
	;;
esac

# pass remaining arguments on
shift $((OPTIND - 1))


echo ""
echo "Creating pkg for macOS installation... <ctrl-c to abort>"
echo ""

if [ -z "$PKG_CODESIGN_KEY" ]; then
    echo "\$PKG_CODESIGN_KEY not set to certificate name, skipping codesign."
fi

if [ -z "$PKG_NOTARIZE_KEY" ]; then
    echo "\$PKG_NOTARIZE_KEY not set to pass-key, skipping notarize."
fi

if [ -z "$PKG_INSTALL_KEY" ]; then
    echo "\$PKG_INSTALL_KEY not set to certificate name, skipping pkg install signing."
fi

version=$(awk < META '{if ($1 == "Version:") print $2;}')
prefix="/usr/local"
if [ -f "${BASE_DIR}/../config.status" ]; then
    prefix=$(grep 'S\["prefix"\]' "${BASE_DIR}/../config.status" | tr '=' ' ' | awk '{print $2;}' | tr -d '"')
fi

echo "Version is $version"
echo "Prefix set to $prefix"
echo "RC, if set: $RC"
echo ""

sleep 3

if [ -z $skip_install ]; then
    rm -rf ${WORKDIR} || true

    mkdir -p ${WORKDIR} || fail "Unable to create $WORKDIR"

    echo "Running \"make install DESTDIR=$WORKDIR\" ... "
    make install DESTDIR="${WORKDIR}" || fail "Make install failed."
fi

echo ""
echo "make install completed."
echo ""

# Make an attempt to figure out where "zpool" is installed to,
# repo default is /usr/local/sbin/ but macOS prefers /usr/local/bin
pushd $WORKDIR || fail "failed to create workdir"
file=$(find usr -type f -name zpool)
bindir=$(dirname "$file")
popd || fail "failed to popd"

codesign_dirs="
${WORKDIR}/Library/Extensions/zfs.kext/
"
codesign_files="
${WORKDIR}/${bindir}/zdb
${WORKDIR}/${bindir}/zed
${WORKDIR}/${bindir}/zfs
${WORKDIR}/${bindir}/zhack
${WORKDIR}/${bindir}/zinject
${WORKDIR}/${bindir}/zpool
${WORKDIR}/${bindir}/zstream
${WORKDIR}/${bindir}/ztest
${WORKDIR}/${bindir}/zfs_ids_to_path
${WORKDIR}/${bindir}/InvariantDisks
${WORKDIR}/${bindir}/zfs_util
${WORKDIR}/${bindir}/zconfigd
${WORKDIR}/${bindir}/zsysctl
${WORKDIR}/${bindir}/mount_zfs
${WORKDIR}/${prefix}/libexec/zfs/zpool_influxdb
${WORKDIR}/${prefix}/lib/libnvpair.a
${WORKDIR}/${prefix}/lib/libuutil.a
${WORKDIR}/${prefix}/lib/libzfs.a
${WORKDIR}/${prefix}/lib/libzpool.a
${WORKDIR}/${prefix}/lib/libzfs_core.a
${WORKDIR}/${prefix}/lib/librt.so.1
${WORKDIR}/${prefix}/lib/libnvpair.?.dylib
${WORKDIR}/${prefix}/lib/libuutil.?.dylib
${WORKDIR}/${prefix}/lib/libzfs.?.dylib
${WORKDIR}/${prefix}/lib/libzpool.?.dylib
${WORKDIR}/${prefix}/lib/libzfs_core.?.dylib
${WORKDIR}/${prefix}/lib/libzfsbootenv.?.dylib
${WORKDIR}/Library/Filesystems/zfs.fs/Contents/Resources/zfs_util
${WORKDIR}/Library/Filesystems/zfs.fs/Contents/Resources/mount_zfs
"

codesign_all="$codesign_files $codesign_dirs"

function fail
{
    echo "$@"
    exit 1
}

function do_unlock
{
    cert=$1

    echo "Looking for certificate ${cert} ..."

    keychain=$(security find-certificate -c "${cert}" | awk '{if ($1 == "keychain:") print $2;}'|tr -d '"')

    echo "Unlocking keychain $keychain ..."
    security unlock-keychain "${keychain}" || fail "Unable to unlock keychain"

    retval=${keychain}
}

function do_codesign
{
    failures=0

    echo ""

    do_unlock "${PKG_CODESIGN_KEY}"

    echo "OS $OS"
    if [ x"$OS" == x"10.12" ] ||
       [ x"$OS" == x"10.11" ] ||
       [ x"$OS" == x"10.10" ] ||
       [ x"$OS" == x"10.9" ]; then
	extra=""
    else
	extra="--options runtime"
    fi

    for file in ${codesign_all}
    do
	echo "$file"
	codesign --timestamp ${extra} -fvs "${PKG_CODESIGN_KEY}" "${file}" || failures=$((failures+1))
    done

    if [ "$failures" -ne 0 ]; then
	echo "codesign phase had $failures issues ..."
	exit 1
    fi

    # Look into where mount_zfs umount_zfs fsck_zfs went ..
    # also: InvariantDisks
}

# Delete everything in a directory, keeping a list
# of files.
# argument 1: directory path "/tmp/dir"
# argument 2: keep list "(item1|item2|tem3)"
function delete_and_keep
{
    # Keep only commands that apply to macOS
    directory="$1"
    keep="$2"

    pushd "${directory}" || fail "Unable to cd to ${directory}"

    shopt -s extglob

    for file in *
    do
	# shellcheck disable=SC2254
	case "${file}" in
	!${keep})
	    echo "Deleting non macOS file \"$file\""
	    rm -f "${file}"
	    ;;
	*)
	    # echo "Not deleting $file"
	esac
    done
    shopt -u extglob
    popd || fail "failed to popd"
}

function do_prune
{

    delete_and_keep "${WORKDIR}/${bindir}/" "(zfs|zpool|zdb|zed|zhack|zinject|zstream|zstreamdump|ztest|InvariantDisks|zfs_util|zconfigd|arc_summary|arcstat|dbufstat|fsck.zfs|zilstat|zfs_ids_to_path|zpool_influxdb|zsysctl|mount_zfs)"

    pushd "${WORKDIR}" || fail "Unable to cd to ${WORKDIR}"

    # Using relative paths here for safety
    rm -rf \
"./${prefix}/share/zfs-macos/runfiles" \
"./${prefix}/share/zfs-macos/test-runner" \
"./${prefix}/share/zfs-macos/zfs-tests" \
"./${prefix}/src"

    popd || fail "failed to popd"
}

# Find any libraries we link with outside of zfs (and system).
# For example, /usr/local/opt/openssl/lib/libssl.1.1.0.dylib
# and copy them into /usr/local/zfs/lib - then update the paths to
# those libraries in all cmds and libraries.
# To do, handle "/usr/local/zfs/" from ./configure arguments (--prefix)
function copy_fix_libraries
{
    echo "Fixing external libraries ... "
    fixlib=$(otool -L ${codesign_files} | egrep '/usr/local/opt/|/opt/local/lib/' |awk '{print $1;}' | grep '\.dylib$' | sort | uniq)

    # Add the libs into codesign list - both to be codesigned, and updated
    # between themselves (libssl depends on libcrypt)
    # copy over, build array of relative paths to add.
    fixlib_relative=""
    for lib in $fixlib
    do
	dir=$(dirname "$lib")
	name=$(basename "$lib" .dylib)
	echo "    working on ${name}.dylib ..."
	rsync -ar --include "${name}*" --exclude="*" "${dir}/" "${WORKDIR}/${prefix}/lib/"
	fixlib_relative="${fixlib_relative} ${WORKDIR}/${prefix}/lib/${name}.dylib"
    done

    echo "Adding in new libraries: "
    echo "${fixlib_relative}"

    # Add new libraries
    codesign_files="${codesign_files} ${fixlib_relative}"
    codesign_all="${codesign_all} ${fixlib_relative}"

    # Fix up paths between binaries and libraries
    for lib in $fixlib
    do
	dir=$(dirname "$lib")
	name=$(basename "$lib" .dylib)

	# We could just change $lib into $prefix, which will work for
	# zfs libraries. But libssl having libcrypto might have a different
	# path, so lookup the source path each time.
	for file in $codesign_files
	do
	    chmod u+w "${file}"
	    src=$(otool -L "$file" | awk '{print $1;}' | grep "${name}.dylib")
	    install_name_tool -change "${src}" "${prefix}/lib/${name}.dylib" "${file}"
	done
    done
}

# Upload .pkg file
# Staple .pkg file
function do_notarize
{
    echo "Uploading PKG to Apple ..."

    TFILE="out-altool.xml"
    RFILE="req-altool.xml"
    xcrun altool --notarize-app -f my_package_new.pkg --primary-bundle-id org.openzfsonosx.zfs -u lundman@lundman.net -p "$PKG_NOTARIZE_KEY" --output-format xml > ${TFILE}

    GUID=$(/usr/libexec/PlistBuddy -c "Print :notarization-upload:RequestUUID" ${TFILE})
    echo "Uploaded. GUID ${GUID}"
    echo "Waiting for Apple to notarize..."
    while true
    do
	sleep 10
	echo "Querying Apple."

	xcrun altool --notarization-info "${GUID}" -u lundman@lundman.net -p "$PKG_NOTARIZE_KEY" --output-format xml > ${RFILE}
	status=$(/usr/libexec/PlistBuddy -c "Print :notarization-info:Status" ${RFILE})
	if [ "$status" != "in progress" ]; then
	    echo "Status: $status ."
	    break
	fi
	echo "Status: $status - sleeping ..."
	sleep 30
    done

    echo "Stapling PKG ..."
    xcrun stapler staple my_package_new.pkg
    ret=$?
    xcrun stapler validate -v my_package_new.pkg

    if [ $ret != 0 ]; then
	echo "Failed to notarize: $ret"
	grep "https://" ${RFILE}
	exit 1
    fi

}

echo "Pruning install area ..."
do_prune

if [ -n "$fix_libraries" ]; then
    copy_fix_libraries
    copy_fix_libraries
fi

if [ -n "$PKG_CODESIGN_KEY" ]; then
    do_codesign
fi

sleep 1

echo "Creating pkg ... "

sign=()

if [ -n "$PKG_INSTALL_KEY" ]; then
    do_unlock "${PKG_INSTALL_KEY}"
    #sign=(--sign "$PKG_INSTALL_KEY" --keychain "$retval" --keychain ~/Library/Keychains/login.keychain-db)
    echo sign=--sign "$PKG_INSTALL_KEY" --keychain "$retval"
    sign=(--sign "$PKG_INSTALL_KEY" --keychain "$retval")
fi

rm -f my_package.pkg
pkgbuild --root "${WORKDIR}" --identifier org.openzfsonosx.zfs --version "${version}" --scripts "${BASE_DIR}/../contrib/macOS/pkg-scripts/" "${sign[@]}" my_package.pkg

ret=$?

echo "pkgbuild result $ret"

if [ $ret != 0 ]; then
    fail "pkgbuild failed"
fi

friendly=$(awk '/SOFTWARE LICENSE AGREEMENT FOR macOS/' '/System/Library/CoreServices/Setup Assistant.app/Contents/Resources/en.lproj/OSXSoftwareLicense.rtf' | awk -F 'macOS ' '{print $NF}' | tr -d '\\')
if [ -z "$friendly" ]; then
    friendly=$(awk '/SOFTWARE LICENSE AGREEMENT FOR OS X/' '/System/Library/CoreServices/Setup Assistant.app/Contents/Resources/en.lproj/OSXSoftwareLicense.rtf' | awk -F 'OS X ' '{print $NF}' | awk '{print substr($0, 0, length($0)-1)}')
fi

friendly=$(echo "$friendly" | tr ' ' '.')

# Now fiddle with pkg to make it nicer
productbuild --synthesize --package ./my_package.pkg distribution.xml

sed < distribution.xml > distribution_new.xml -e \
"s#</installer-gui-script># <title>Open ZFS on OsX ${version} - ${friendly}-${OS}</title>\\
 <background file=\"background.png\" scaling=\"proportional\" alignment=\"bottomleft\"/> \\
 <volume-check>\\
  <allowed-os-versions>\\
   <os-version min=\"${OS}.0\"/>\\
  </allowed-os-versions>\\
 </volume-check>\\
 <installation-check script=\"installation_check()\">\\
  <ram min-gb=\"2.00\"/>\\
 </installation-check>\\
 <welcome file=\"Welcome.rtf\"/>\\
 <readme file=\"ReadMe.rtf\"/>\\
 <license file=\"License.txt\"/>\\
 <conclusion file=\"Conclusion.rtf\"/>\\
<options rootVolumeOnly=\"true\" customize=\"never\" allow-external-scripts=\"true\"/>\\
\\
<script>\\
SCRIPT_REPLACE \\
</script>\\
\\
</installer-gui-script>#g"

sed -i "" \
    -e "/SCRIPT_REPLACE/{r ${BASE_DIR}/../contrib/macOS/resources/javascript.js" \
    -e 'd' -e '}' \
    distribution_new.xml

rm -f my_package_new.pkg
productbuild --distribution distribution_new.xml --resources "${BASE_DIR}/../contrib/macOS/resources/"  --scripts "${BASE_DIR}/../contrib/macOS/product-scripts" "${sign[@]}" --package-path ./my_package.pkg my_package_new.pkg

if [ -n "$PKG_NOTARIZE_KEY" ]; then
    SECONDS=0
    do_notarize
    echo "Notarize took $SECONDS seconds to complete"
fi

arch=$(uname -m)
if [ x"$arch" == x"arm64" ]; then
   name="OpenZFSonOsX-${version}${RC}-${friendly}-${OS}-${arch}.pkg"
else
   name="OpenZFSonOsX-${version}${RC}-${friendly}-${OS}.pkg"
fi

mv my_package_new.pkg "${name}"
ls -l "${name}"

# Cleanup
rm -f my_package.pkg distribution.xml distribution_new.xml req-altool.xml out-altool.xml

echo "All done."
