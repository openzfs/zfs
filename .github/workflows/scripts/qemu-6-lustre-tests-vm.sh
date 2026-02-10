#!/usr/bin/env bash

######################################################################
# 6) Test if Lustre can still build against ZFS
######################################################################
set -e

# Build from the latest Lustre tag rather than the master branch.  We do this
# under the assumption that master is going to have a lot of churn thus will be
# more prone to breaking the build than a point release.  We don't want ZFS
# PR's reporting bad test results simply because upstream Lustre accidentally
# broke their build.
#
# Skip any RC tags, or any tags where the last version digit is 50 or more.
# Versions with 50 or more are development versions of Lustre.
repo=https://github.com/lustre/lustre-release.git
tag="$(git ls-remote --refs --exit-code --sort=version:refname --tags $repo | \
	awk -F '_' '/-RC/{next}; /refs\/tags\/v/{if ($NF < 50){print}}' | \
	tail -n 1 | sed 's/.*\///')"

echo "Cloning Lustre tag $tag"
git clone --depth 1 --branch "$tag" "$repo"

cd lustre-release

# Include Lustre patches to build against master/zfs-2.4.x.  Once these
# patches are merged we can remove these lines.
patches=('https://review.whamcloud.com/changes/fs%2Flustre-release~62101/revisions/2/patch?download'
	'https://review.whamcloud.com/changes/fs%2Flustre-release~63267/revisions/9/patch?download')

for p in "${patches[@]}" ; do
	curl $p | base64 -d > patch
	patch -p1 < patch || true
done

echo "Configure Lustre"
./autogen.sh
# EL 9 needs '--disable-gss-keyring'
./configure --with-zfs --disable-gss-keyring
echo "Building Lustre RPMs"
make rpms
ls *.rpm

# There's only a handful of Lustre RPMs we actually need to install
lustrerpms="$(ls *.rpm | grep -E 'kmod-lustre-osd-zfs-[0-9]|kmod-lustre-[0-9]|lustre-osd-zfs-mount-[0-9]')"
echo "Installing: $lustrerpms"
sudo dnf -y install $lustrerpms
sudo modprobe -v lustre

# Should see some Lustre lines in dmesg
sudo dmesg | grep -Ei 'lnet|lustre'
