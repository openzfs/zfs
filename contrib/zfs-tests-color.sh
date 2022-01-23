#!/bin/bash

# A quick and dirty script for providing a bit of color and contrast
# in zfs-tests.sh output

# We don't want while read LINE to eat our leading whitespace
IFS=''
if [[ "$(uname -s)" == "FreeBSD" ]]; then
	SED=gsed
else
	SED=sed
fi
while read LINE; do
	res=$(echo "${LINE}" | $SED \
		-e s/'\] \[PASS\]$'/'\] \[\\e\[92mPASS\\e\[0m\]'/ \
		-e s/'\] \[FAIL\]$'/'\] \[\\e\[1;91mFAIL\\e\[0m\]'/ \
		-e s/'\] \[KILLED\]$'/'\] \[\\e\[1;101mKILLED\\e\[0m\]'/ \
		-e s/'\] \[SKIP\]$'/'\] \[\\e\[1mSKIP\\e\[0m\]'/ \
		-e s/'\] \[RERAN\]$'/'\] \[\\e\[1;93mRERAN\\e\[0m\]'/ \
		-e s/'^\(PASS\W\)'/'\\e\[92m\1\\e\[0m'/ \
		-e s/'^\(FAIL\W\)'/'\\e\[1;91m\1\\e\[0m'/ \
		-e s/'^\(KILLED\W\)'/'\\\e\[1;101m\1\\e\[0m'/ \
		-e s/'^\(SKIP\W\)'/'\\e\[1m\1\\e\[0m'/ \
		-e s/'^\(RERAN\W\)'/'\\e\[1;93m\1\\e\[0m'/ \
		-e s/'^Tests\ with\ result\(.\+\)PASS\(.\+\)$'/'Tests with result\1\\e\[92mPASS\\e\[0m\2'/ \
		-e s/'^\(\W\+\)\(KILLED\)\(\W\)'/'\1\\e\[1;101m\2\\e\[0m\3'/g \
		-e s/'^\(\W\+\)\(FAIL\)\(\W\)'/'\1\\e\[1;91m\2\\e\[0m\3'/g \
		-e s/'^\(\W\+\)\(RERUN\)\(\W\)'/'\1\\e\[1;93m\2\\e\[0m\3'/g \
		-e s/'^\(\W\+\)\(SKIP\)\(\W\)'/'\1\\e\[1m\2\\e\[0m\3'/g \
		-e s/'expected \(PASS\))$'/'expected \\e\[92m\1\\e\[0m)'/ \
		-e s/'expected \(KILLED\))$'/'expected \\e\[1;101m\1\\e\[0m)'/ \
		-e s/'expected \(FAIL\))$'/'expected \\e\[1;91m\1\\e\[0m)'/ \
		-e s/'expected \(RERUN\))$'/'expected \\e\[1;93m\1\\e\[0m)'/ \
		-e s/'expected \(SKIP\))$'/'expected \\e\[1m\1\\e\[0m)'/ \
		-e s/'^Test\( ([A-Za-z0-9 ]\+)\)\?: \(.\+\) (run as \(.\+\)) \[\([0-9]\+:[0-9]\+\)\] \[\(.\+\)\]$'/'\\e\[1mTest\1: \\e\[0m\2 (run as \\e\[1m\3\\e\[0m) \[\\e\[1m\4\\e\[0m\] \[\5\]'/ \
	)
	echo -e "${res}"
done