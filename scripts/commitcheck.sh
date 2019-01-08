#!/bin/bash

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
function check_tagged_line_with_url ()
{
    regex='^\s*'"$1"':\s\K([[:graph:]]+)$'
    foundline=$(git log -n 1 "$REF" | grep -Po "$regex")
    if [ -z "$foundline" ]; then
        echo "error: missing \"$1\""
        return 1
    fi

    if ! test_url "$foundline"; then
        return 1
    fi

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
    body=$(git log -n 1 --pretty=%b "$REF" | grep -E -m 1 '.{73}')
    if [ -n "$body" ]; then
        echo "error: commit message body contains line over 72 characters"
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
    # subject starts with OpenZFS dddd
    subject=$(git log -n 1 --pretty=%s "$REF" | grep -E -m 1 '^OpenZFS [[:digit:]]+ - ')
    if [ -z "$subject" ]; then
        echo "OpenZFS patch ports must have a summary that starts with \"OpenZFS dddd - \""
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

    # need a approved by line
    if ! check_tagged_line "Approved by" ; then
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

# have a normal commit
if ! new_change_commit ; then
    exit 1
fi

exit 0
