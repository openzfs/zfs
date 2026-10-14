#!/bin/ksh

# Commands to perform failsafe-critical cleanup after a test is killed.
#
# This should only be used to ensure the system is restored to a functional
# state in the event of tests being killed (preventing normal cleanup).

# sudo replaces PATH with its secure_path: use the test suite's path, as
# libtest.shlib does, so that a zinject not installed there is found.
if [ -n "$STF_PATH" ]; then
	export PATH="$STF_PATH"
fi

zinject -c all
