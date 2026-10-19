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

. $STF_SUITE/include/libtest.shlib

# A killed test cannot restore the tunables it saved with save_tunable, so
# restore them here, before the next test runs.  Tunables that a group's
# setup saved with save_group_tunable are left for its cleanup.
for f in $TEST_BASE_DIR/tunable-*; do
	[[ -e $f ]] && restore_tunable ${f##*/tunable-}
done

zinject -c all
