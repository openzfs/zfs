#!/bin/sh

REF="HEAD"

# test commit body for length
# lines containing urls are exempt for the length limit.
test_commit_bodylength()
{
    length="72"
    body=$(git log --no-show-signature -n 1 --pretty=%b "$REF" | grep -Ev "http(s)*://" | grep -E -m 1 ".{$((length + 1))}")
    if [ -n "$body" ]; then
        echo "error: commit message body contains line over ${length} characters"
        return 1
    fi

    return 0
}

# check for a tagged line
check_tagged_line()
{
    regex='^[[:space:]]*'"$1"':[[:space:]][[:print:]]+[[:space:]]<[[:graph:]]+>$'
    foundline=$(git log --no-show-signature -n 1 "$REF" | grep -E -m 1 "$regex")
    if [ -z "$foundline" ]; then
        echo "error: missing \"$1\""
        return 1
    fi

    return 0
}

# check commit message for a normal commit
new_change_commit()
{
    error=0

    # subject is not longer than 72 characters
    long_subject=$(git log --no-show-signature -n 1 --pretty=%s "$REF" | grep -E -m 1 '.{73}')
    if [ -n "$long_subject" ]; then
        echo "error: commit subject over 72 characters"
        error=1
    fi

    # need a signed off by
    if ! check_tagged_line "Signed-off-by" ; then
        error=1
    fi

    # ensure that no lines in the body of the commit are over 72 characters
    if ! test_commit_bodylength ; then
        error=1
    fi

    return "$error"
}

is_coverity_fix()
{
    # subject starts with Fix coverity defects means it's a coverity fix
    subject=$(git log --no-show-signature -n 1 --pretty=%s "$REF" | grep -E -m 1 '^Fix coverity defects')
    if [ -n "$subject" ]; then
        return 0
    fi

    return 1
}

coverity_fix_commit()
{
    error=0

    # subject starts with Fix coverity defects: CID dddd, dddd...
    subject=$(git log --no-show-signature -n 1 --pretty=%s "$REF" |
        grep -E -m 1 'Fix coverity defects: CID [[:digit:]]+(, [[:digit:]]+)*')
    if [ -z "$subject" ]; then
        echo "error: Coverity defect fixes must have a subject line that starts with \"Fix coverity defects: CID dddd\""
        error=1
    fi

    # need a signed off by
    if ! check_tagged_line "Signed-off-by" ; then
        error=1
    fi

    # test each summary line for the proper format
    OLDIFS=$IFS
    IFS='
'
    for line in $(git log --no-show-signature -n 1 --pretty=%b "$REF" | grep -E '^CID'); do
        if ! echo "$line" | grep -qE '^CID [[:digit:]]+: ([[:graph:]]+|[[:space:]])+ \(([[:upper:]]|\_)+\)'; then
            echo "error: commit message has an improperly formatted CID defect line"
            error=1
        fi
    done
    IFS=$OLDIFS

    # ensure that no lines in the body of the commit are over 72 characters
    if ! test_commit_bodylength; then
        error=1
    fi

    return "$error"
}

if [ -n "$1" ]; then
    REF="$1"
fi

# if coverity fix, test against that
if is_coverity_fix; then
    if ! coverity_fix_commit; then
        exit 1
    else
        exit 0
    fi
fi

# have a normal commit
if ! new_change_commit ; then
    exit 1
fi

exit 0
