#!/usr/bin/env python3

"""
Find "failing test -> fix" commit pairs in a range of commits.

A PR in this format adds a test that demonstrates a bug in one commit, then
fixes the bug in a later commit.  The test commit only touches files under
tests/, and so should build and fail on its own; the PR head should pass.

Usage: failfirst-detect.py BASE HEAD

A test commit is a non-merge commit in BASE..HEAD that:
- changes only files under tests/,
- adds or modifies at least one test script (tests/zfs-tests/tests/**.ksh,
  other than setup.ksh and cleanup.ksh), or a channel program (.zcp) whose
  .ksh test of the same name exists, and
- is followed by at least one commit that changes files outside tests/.

Prints a JSON object to stdout:

  {"found": true|false,
   "labels": ["Fails without fix: SHA9 TITLE", ...,
              "Passes with fix: SHA9 TITLE"],
   "jobs": {LABEL: {"role": "test", "ref": SHA, "expect": "fail",
                    "tests": "...", "title": "..."},
            ...,
            LABEL: {"role": "head", "ref": HEAD_SHA, "expect": "pass",
                    "tests": "...", "title": "..."}},
   "summary": "markdown text"}

The labels are the verify job's only matrix key, so that GitHub shows them
in the job names; the jobs are looked up by label.

"tests" is a space separated list of test paths relative to the test suite
(e.g. "tests/functional/mmap/mmap_eof_extend.ksh"), as zfs-tests.sh -t
expects.  The "head" entry runs every test from every test commit that still
exists at HEAD.
"""

import json
import os
import re
import subprocess
import sys

# Maximum number of test commits to verify, to bound the CI time.
MAX_TEST_COMMITS = 4

# Paths are passed to shell scripts, so only plain names are accepted.
TEST_SCRIPT_RE = re.compile(r'^tests/zfs-tests/(tests/[A-Za-z0-9_./-]+\.ksh)$')
# A channel program (.zcp) is run by the .ksh test of the same name.
ZCP_RE = re.compile(r'^tests/zfs-tests/(tests/[A-Za-z0-9_./-]+)\.zcp$')
NOT_A_TEST = ('setup.ksh', 'cleanup.ksh')


def git(*args):
    return subprocess.run(['git'] + list(args), check=True,
                          capture_output=True, text=True).stdout


def changed_files(sha):
    """Return [(status, path)] for a commit against its first parent."""
    out = git('diff-tree', '--root', '--no-commit-id', '-r', '-M',
              '--name-status', '-z', sha)
    fields = out.split('\0')
    files = []
    i = 0
    while i < len(fields) and fields[i]:
        status = fields[i]
        if status[0] in 'RC':
            files.append((status[0], fields[i + 1]))   # old path
            files.append((status[0], fields[i + 2]))   # new path
            i += 3
        else:
            files.append((status[0], fields[i + 1]))
            i += 2
    return files


def tests_in(sha, files):
    """Test scripts added or modified (not deleted) by a commit."""
    tests = []
    for status, path in files:
        if status == 'D':
            continue
        m = TEST_SCRIPT_RE.match(path)
        if m and os.path.basename(path) not in NOT_A_TEST:
            test = m.group(1)
        else:
            m = ZCP_RE.match(path)
            if not m or not exists_at(sha, m.group(1) + '.ksh'):
                continue
            test = m.group(1) + '.ksh'
        if test not in tests:
            tests.append(test)
    return tests


def exists_at(sha, test):
    return subprocess.run(['git', 'cat-file', '-e',
                           f'{sha}:tests/zfs-tests/{test}'],
                          capture_output=True).returncode == 0


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    base, head = sys.argv[1], git('rev-parse', sys.argv[2]).strip()

    commits = git('rev-list', '--reverse', '--no-merges',
                  f'{base}..{head}').split()
    info = []
    for sha in commits:
        files = changed_files(sha)
        only_tests = bool(files) and \
            all(path.startswith('tests/') for _, path in files)
        info.append({
            'sha': sha,
            'title': git('log', '-1', '--format=%s', sha).strip(),
            'only_tests': only_tests,
            'tests': tests_in(sha, files) if only_tests else [],
        })

    pairs = []
    for i, c in enumerate(info):
        if not c['tests']:
            continue
        fix = next((d for d in info[i + 1:] if not d['only_tests']), None)
        if fix is not None:
            pairs.append((c, fix))

    lines = []
    include = []
    if not pairs:
        lines.append('No "failing test -> fix" commit pairs found in '
                     f'{len(commits)} commit(s).')
    else:
        if len(pairs) > MAX_TEST_COMMITS:
            lines.append(f'Found {len(pairs)} test commits; verifying the '
                         f'first {MAX_TEST_COMMITS} only.')
            pairs = pairs[:MAX_TEST_COMMITS]
        lines.append('| Test commit | Fixed by | Tests |')
        lines.append('|---|---|---|')
        all_tests = []
        for c, fix in pairs:
            include.append({
                'role': 'test',
                'ref': c['sha'],
                'expect': 'fail',
                'tests': ' '.join(c['tests']),
                'title': c['title'],
            })
            lines.append(f"| {c['sha'][:9]} {c['title']} | "
                         f"{fix['sha'][:9]} {fix['title']} | "
                         f"{'<br>'.join(c['tests'])} |")
            all_tests += [t for t in c['tests'] if t not in all_tests]
        head_tests = [t for t in all_tests if exists_at(head, t)]
        if head_tests:
            include.append({
                'role': 'head',
                'ref': head,
                'expect': 'pass',
                'tests': ' '.join(head_tests),
                'title': git('log', '-1', '--format=%s', head).strip(),
            })
        for t in all_tests:
            if t not in head_tests:
                lines.append(f'Note: {t} no longer exists at HEAD and is '
                             'not run there.')

    jobs = {label(j): j for j in include}
    print(json.dumps({
        'found': bool(include),
        'labels': list(jobs),
        'jobs': jobs,
        'summary': '\n'.join(lines),
    }))


def label(job):
    """Name of a verify job, as shown in the PR's checks."""
    title = job['title']
    if len(title) > 60:
        title = title[:57] + '...'
    what = 'Fails without fix' if job['expect'] == 'fail' \
        else 'Passes with fix'
    return f"{what}: {job['ref'][:9]} {title}"


if __name__ == '__main__':
    main()
