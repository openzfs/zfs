#!/bin/ksh -p

source "${STF_SUITE}/include/libtest.shlib"
source "${STF_SUITE}/tests/functional/mv_files/mv_files.cfg"

# This will test the #7401 regression.
log_assert "Check that creating many files quickly is safe"

DIR="${TESTDIR}/RANDOM_SMALL"

log_must mkdir "${DIR}"

count=0
for i in $(range_shuffle 1 "${RC_PASS1}") ; do
    if ! touch "${DIR}/${i}" ; then
	    log_fail "error creating ${i} after ${count} files"
    fi
    count=$((count+1))
done

visible="$(find "${DIR}" -type f|wc -l)"

log_must [ "${visible}" -eq "${RC_PASS1}" ]

log_assert "Check that creating them in another order is safe"

DIR1="${TESTDIR}/RANDOM2"

log_must mv "${DIR}" "${DIR1}"

log_must mkdir "${DIR}"

count=0
for i in $(cd "${DIR1}" ; ls -U . ) ; do
    if ! touch "${DIR}/${i}" ; then
	    log_fail "error creating ${i} after ${count} files"
    fi
    count=$((count+1))
    [ "${count}" -eq "${RC_PASS2}" ] && break
done

visible="$(find "${DIR}" -type f|wc -l)"

if [ "${visible}" -eq "${RC_PASS2}" ] ; then
    log_pass "Created all ${RC_PASS2} files"
else
    log_fail "Number of created files ${visible} is not ${RC_PASS2}"
fi
