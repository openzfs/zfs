#!/usr/bin/env python3

"""
Determine the CI type based on the change list and commit message.

Prints "quick" if (explicity required by user):
- the *last* commit message contains 'ZFS-CI-Type: quick'
or if (heuristics):
- the files changed are not in the list of specified directories, and
- all commit messages do not contain 'ZFS-CI-Type: full'

Otherwise prints "full".
"""

import sys
import subprocess
import re

"""
Patterns of files that are not considered to trigger full CI.
Note: not using pathlib.Path.match() because it does not support '**'
"""
FULL_RUN_IGNORE_REGEX = list(map(re.compile, [
    r'.*\.md',
    r'.*\.gitignore'
]))

"""
Patterns of files that are considered to trigger full CI.
"""
FULL_RUN_REGEX = list(map(re.compile, [
    r'cmd.*',
    r'configs/.*',
    r'META',
    r'.*\.am',
    r'.*\.m4',
    r'autogen\.sh',
    r'configure\.ac',
    r'copy-builtin',
    r'contrib',
    r'etc',
    r'include',
    r'lib/.*',
    r'module/.*',
    r'scripts/.*',
    r'tests/.*',
    r'udev/.*'
]))

if __name__ == '__main__':

    prog = sys.argv[0]

    if len(sys.argv) != 3:
        print(f'Usage: {prog} <head_ref> <base_ref>')
        sys.exit(1)

    head, base = sys.argv[1:3]

    def output_type(type, reason):
        print(f'{prog}: will run {type} CI: {reason}', file=sys.stderr)
        print(type)
        sys.exit(0)

    # check last (HEAD) commit message
    last_commit_message_raw = subprocess.run([
        'git', 'show', '-s', '--format=%B', 'HEAD'
    ], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    for line in last_commit_message_raw.stdout.decode().splitlines():
        if line.strip().lower() == 'zfs-ci-type: quick':
            output_type('quick', f'explicitly requested by HEAD commit {head}')

    # check all commit messages
    all_commit_message_raw = subprocess.run([
        'git', 'show', '-s',
        '--format=ZFS-CI-Commit: %H%n%B', f'{head}...{base}'
    ], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    all_commit_message = all_commit_message_raw.stdout.decode().splitlines()

    commit_ref = head
    for line in all_commit_message:
        if line.startswith('ZFS-CI-Commit:'):
            commit_ref = line.lstrip('ZFS-CI-Commit:').rstrip()
        if line.strip().lower() == 'zfs-ci-type: full':
            output_type('full', f'explicitly requested by commit {commit_ref}')

    # check changed files
    changed_files_raw = subprocess.run([
        'git', 'diff', '--name-only', head, base
    ], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    changed_files = changed_files_raw.stdout.decode().splitlines()

    for f in changed_files:
        for r in FULL_RUN_IGNORE_REGEX:
            if r.match(f):
                break
        else:
            for r in FULL_RUN_REGEX:
                if r.match(f):
                    output_type(
                        'full',
                        f'changed file "{f}" matches pattern "{r.pattern}"'
                        )

    # catch-all
    output_type('quick', 'no changed file matches full CI patterns')
