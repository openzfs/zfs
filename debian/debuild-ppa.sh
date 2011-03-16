#!/bin/bash
#
# Launchpad PPA build helper.
#

PPA_USER=${PPA_USER:-$(whoami)}
PPA_NAME=zfs
PPA_DISTRIBUTION_LIST='lucid maverick natty'

export EDITOR='ed -s -p'

if [ ! -d debian/ ]
then
	echo 'Error: The debian/ directory is not in the current working path.'
	exit 1
fi

for ii in $PPA_DISTRIBUTION_LIST
do
	# Change the first line of the debian/changelog file
	# from: MyPackage (1.2.3-4) unstable; urgency=low
	# to: MyPackage (1.2.3-4~distname) distname; urgency=low
	debchange --local="~$ii" --distribtuion="$ii" dummy
	sed -i -e '2,7d' debian/changelog

	debuild -S -sd
	git checkout debian/changelog
done
