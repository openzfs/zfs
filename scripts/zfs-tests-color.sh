#!/bin/sh
# A large mass of sed for coloring zfs-tests.sh output
# Version 2, thanks to наб.
# Just pipe zfs-tests.sh output into this, and watch.

exec "$(command -v gsed || echo sed)" \
	-e 's/\] \[PASS\]$/] [\x1b[92mPASS\x1b[0m]/' \
	-e 's/\] \[FAIL\]$/] [\x1b[1;91mFAIL\x1b[0m]/' \
	-e 's/\] \[KILLED\]$/] [\x1b[1;101mKILLED\x1b[0m]/' \
	-e 's/\] \[SKIP\]$/] [\x1b[1mSKIP\x1b[0m]/' \
	-e 's/\] \[RERAN\]$/] [\x1b[1;93mRERAN\x1b[0m]/' \
	-e 's/^\(PASS\W\)/\x1b[92m\1\x1b[0m/' \
	-e 's/^\(FAIL\W\)/\x1b[1;91m\1\x1b[0m/' \
	-e 's/^\(KILLED\W\)/\x1b[1;101m\1\x1b[0m/' \
	-e 's/^\(SKIP\W\)/\x1b[1m\1\x1b[0m/' \
	-e 's/^\(RERAN\W\)/\x1b[1;93m\1\x1b[0m/' \
	-e 's/^Tests with result\(.\+\)PASS\(.\+\)$/Tests with result\1\x1b[92mPASS\x1b[0m\2/' \
	-e 's/^\(\W\+\)\(KILLED\)\(\W\)/\1\x1b[1;101m\2\x1b[0m\3/g' \
	-e 's/^\(\W\+\)\(FAIL\)\(\W\)/\1\x1b[1;91m\2\x1b[0m\3/g' \
	-e 's/^\(\W\+\)\(RERUN\)\(\W\)/\1\x1b[1;93m\2\x1b[0m\3/g' \
	-e 's/^\(\W\+\)\(SKIP\)\(\W\)/\1\x1b[1m\2\x1b[0m\3/g' \
	-e 's/expected \(PASS\))$/expected \x1b[92m\1\x1b[0m)/' \
	-e 's/expected \(KILLED\))$/expected \x1b[1;101m\1\x1b[0m)/' \
	-e 's/expected \(FAIL\))$/expected \x1b[1;91m\1\x1b[0m)/' \
	-e 's/expected \(RERUN\))$/expected \x1b[1;93m\1\x1b[0m)/' \
	-e 's/expected \(SKIP\))$/expected \x1b[1m\1\x1b[0m)/' \
	-e 's/^Test\( ([[:alnum:] ]\+)\)\?: \(.\+\) (run as \(.\+\)) \[\([0-9]\+:[0-9]\+\)\] \[\(.\+\)\]$/\x1b[1mTest\1: \x1b[0m\2 (run as \x1b[1m\3\x1b[0m) [\x1b[1m\4\x1b[0m\] [\5\]/'
