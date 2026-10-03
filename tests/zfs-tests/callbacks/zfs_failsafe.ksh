#!/bin/ksh

# Commands to perform failsafe-critical cleanup after a test is killed.
#
# This should only be used to ensure the system is restored to a functional
# state in the event of tests being killed (preventing normal cleanup).

. $STF_SUITE/include/libtest.shlib

# A killed test cannot restore the tunables it saved with save_tunable, so
# restore them here, before the next test runs.
for f in $TEST_BASE_DIR/tunable-*; do
	[[ -e $f ]] && restore_tunable ${f##*/tunable-}
done

zinject -c all
