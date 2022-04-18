#!/bin/bash

#
# This script can be used to invoke OpenZFS build from native Debian
# packaging.
#

print_help ()
{
	echo "Usage: $(basename $0) [OPTIONS]"
	echo
	echo "Options:"
	echo " -b, --build		Build OpenZFS from Debian Packaging"
	echo " -c, --clean		Clean the workspace"
}

if [ "$#" -ne 1 ]; then
	print_help
	exit 1
fi

case $1 in
	-b|--build)
		cp -r contrib/debian debian
		debuild -i -us -uc -b && fakeroot debian/rules override_dh_binary-modules
		;;
	-c|--clean)
		fakeroot debian/rules override_dh_auto_clean
		rm -rf debian
		;;
	*)
		print_help
		;;
esac

exit 0
