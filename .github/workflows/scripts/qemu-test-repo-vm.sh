#!/bin/bash
#
# Do a test install of ZFS from an external repository.
#
# USAGE:
#
# 	./qemu-test-repo-vm [URL]
#
# URL:		URL to use instead of http://download.zfsonlinux.org
#		If blank, use the default repo from zfs-release RPM.

set -e

source /etc/os-release
OS="$ID"
VERSION="$VERSION_ID"

ALTHOST=""
if [ -n "$1" ] ; then
	ALTHOST="$1"
fi

# Write summary to /tmp/repo so our artifacts scripts pick it up
mkdir /tmp/repo
SUMMARY=/tmp/repo/$OS-$VERSION-summary.txt

# $1: Repo 'zfs' 'zfs-kmod' 'zfs-testing' 'zfs-testing-kmod'
# $2: (optional) Alternate host than 'http://download.zfsonlinux.org' to
#     install from.  Blank means use default from zfs-release RPM.
function test_install {
	repo=$1
	host=""
	if [ -n "$2" ] ; then
		host=$2
	fi

	args="--disablerepo=zfs --enablerepo=$repo"

	# If we supplied an alternate repo URL, and have not already edited
	# zfs.repo, then update the repo file.
	if [ -n "$host" ] && ! grep -q $host /etc/yum.repos.d/zfs.repo ; then
		sudo sed -i "s;baseurl=http://download.zfsonlinux.org;baseurl=$host;g" /etc/yum.repos.d/zfs.repo
	fi

	if ! sudo dnf -y install $args zfs zfs-test ; then
		echo "$repo ${package}...[FAILED] $baseurl" >> $SUMMARY
		return
	fi

	# Load modules and create a simple pool as a sanity test.
	sudo /usr/share/zfs/zfs.sh -r
	truncate -s 100M /tmp/file
	sudo zpool create tank /tmp/file
	sudo zpool status

	# Print out repo name, rpm installed (kmod or dkms), and repo URL
	baseurl=$(grep -A 5 "\[$repo\]" /etc/yum.repos.d/zfs.repo  | awk -F'=' '/baseurl=/{print $2; exit}')
	package=$(sudo rpm -qa | grep zfs | grep -E 'kmod|dkms')

	echo "$repo $package $baseurl" >> $SUMMARY

	sudo zpool destroy tank
	sudo rm /tmp/file
	sudo dnf -y remove zfs
}

echo "##[group]Installing from repo"
# The openzfs docs are the authoritative instructions for the install.  Use
# the specific version of zfs-release RPM it recommends.
case $OS in
almalinux*)
	url='https://raw.githubusercontent.com/openzfs/openzfs-docs/refs/heads/master/docs/Getting%20Started/RHEL-based%20distro/index.rst'
	name=$(curl -Ls $url | grep 'dnf install' | grep -Eo 'zfs-release-[0-9]+-[0-9]+')
	sudo dnf -y install https://zfsonlinux.org/epel/$name$(rpm --eval "%{dist}").noarch.rpm 2>&1
	sudo rpm -qi zfs-release
	for i in zfs zfs-kmod zfs-testing zfs-testing-kmod zfs-latest \
		zfs-latest-kmod zfs-legacy zfs-legacy-kmod zfs-2.2 \
		zfs-2.2-kmod zfs-2.3 zfs-2.3-kmod ; do
		test_install $i $ALTHOST
	done
	;;
fedora*)
	url='https://raw.githubusercontent.com/openzfs/openzfs-docs/refs/heads/master/docs/Getting%20Started/Fedora/index.rst'
	name=$(curl -Ls $url | grep 'dnf install' | grep -Eo 'zfs-release-[0-9]+-[0-9]+')
	sudo dnf -y install -y https://zfsonlinux.org/fedora/$name$(rpm --eval "%{dist}").noarch.rpm
	for i in zfs zfs-latest zfs-legacy zfs-2.2 zfs-2.3 ; do
		test_install $i $ALTHOST
	done
	;;
esac
echo "##[endgroup]"

# Write out a simple version of the summary here. Later on we will collate all
# the summaries and put them into a nice table in the workflow Summary page.
echo "Summary: "
cat $SUMMARY
