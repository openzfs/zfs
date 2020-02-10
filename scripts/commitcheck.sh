#!/usr/bin/env bash

REF="HEAD"

# test a url
function test_url()
{
    url="$1"
    if ! curl --output /dev/null --max-time 60 \
		--silent --head --fail "$url" ; then
        echo "\"$url\" is unreachable"
        return 1
    fi

    return 0
}

# test commit body for length
# lines containing urls are exempt for the length limit.
function test_commit_bodylength()
{
    length="72"
    body=$(git log -n 1 --pretty=%b "$REF" | grep -Ev "http(s)*://" | grep -E -m 1 ".{$((length + 1))}")
    if [ -n "$body" ]; then
        echo "error: commit message body contains line over ${length} characters"
        return 1
    fi

    return 0
}

# check for a tagged line
function check_tagged_line()
{
    regex='^\s*'"$1"':\s[[:print:]]+\s<[[:graph:]]+>$'
    foundline=$(git log -n 1 "$REF" | grep -E -m 1 "$regex")
    if [ -z "$foundline" ]; then
        echo "error: missing \"$1\""
        return 1
    fi

    return 0
}

# check for a tagged line and check that the link is valid
function check_tagged_line_with_url()
{
    regex='^\s*'"$1"':\s\K([[:graph:]]+)$'
    foundline=$(git log -n 1 "$REF" | grep -Po "$regex")
    if [ -z "$foundline" ]; then
        echo "error: missing \"$1\""
        return 1
    fi

    OLDIFS=$IFS
    IFS=$'\n'
    for url in $(echo -e "$foundline"); do
        if ! test_url "$url"; then
            return 1
        fi
    done
    IFS=$OLDIFS

    return 0
}

# check commit message for a normal commit
function new_change_commit()
{
    error=0

    # subject is not longer than 72 characters
    long_subject=$(git log -n 1 --pretty=%s "$REF" | grep -E -m 1 '.{73}')
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

    return $error
}

function is_openzfs_port()
{
    # subject starts with OpenZFS means it's an openzfs port
    subject=$(git log -n 1 --pretty=%s "$REF" | grep -E -m 1 '^OpenZFS')
    if [ -n "$subject" ]; then
        return 0
    fi

    return 1
}

function openzfs_port_commit()
{
    error=0

    # subject starts with OpenZFS dddd
    subject=$(git log -n 1 --pretty=%s "$REF" | grep -E -m 1 '^OpenZFS [[:digit:]]+(, [[:digit:]]+)* - ')
    if [ -z "$subject" ]; then
        echo "error: OpenZFS patch ports must have a subject line that starts with \"OpenZFS dddd - \""
        error=1
    fi

    # need an authored by line
    if ! check_tagged_line "Authored by" ; then
        error=1
    fi

    # need a reviewed by line
    if ! check_tagged_line "Reviewed by" ; then
        error=1
    fi

    # need ported by line
    if ! check_tagged_line "Ported-by" ; then
        error=1
    fi

    # need a url to openzfs commit and it should be valid
    if ! check_tagged_line_with_url "OpenZFS-commit" ; then
        error=1
    fi

    # need a url to illumos issue and it should be valid
    if ! check_tagged_line_with_url "OpenZFS-issue" ; then
        error=1
    fi

    return $error
}

function is_coverity_fix()
{
    # subject starts with Fix coverity defects means it's a coverity fix
    subject=$(git log -n 1 --pretty=%s "$REF" | grep -E -m 1 '^Fix coverity defects')
    if [ -n "$subject" ]; then
        return 0
    fi

    return 1
}

function coverity_fix_commit()
{
    error=0

    # subject starts with Fix coverity defects: CID dddd, dddd...
    subject=$(git log -n 1 --pretty=%s "$REF" |
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
    IFS=$'\n'
    for line in $(git log -n 1 --pretty=%b "$REF" | grep -E '^CID'); do
        echo "$line" | grep -E '^CID [[:digit:]]+: ([[:graph:]]+|[[:space:]])+ \(([[:upper:]]|\_)+\)' > /dev/null
        # shellcheck disable=SC2181
        if [[ $? -ne 0 ]]; then
            echo "error: commit message has an improperly formatted CID defect line"
            error=1
        fi
    done
    IFS=$OLDIFS

    # ensure that no lines in the body of the commit are over 72 characters
    if ! test_commit_bodylength; then
        error=1
    fi

    return $error
}

if [ -n "$1" ]; then
    REF="$1"
fi

# if openzfs port, test against that
if is_openzfs_port; then
    if ! openzfs_port_commit ; then
        exit 1
    else
        exit 0
    fi
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
