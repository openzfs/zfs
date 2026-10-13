#!/usr/bin/env python3

"""Exercise precheck selection and expectations with real commit histories."""

import json
from pathlib import Path
import subprocess
import tempfile
import unittest


DETECT = Path(__file__).resolve().parents[1] / 'failfirst-detect.py'
TEST = 'tests/zfs-tests/tests/functional/example/check.ksh'


class FailfirstDetectTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.repo = Path(self.directory.name)
        self.git('init', '-q')
        self.git('config', 'user.name', 'CI Test')
        self.git('config', 'user.email', 'ci-test@localhost')
        self.write('helper.sh', 'exit 1\n')
        self.base = self.commit('Initial implementation')

    def git(self, *args):
        return subprocess.check_output(['git', *args], cwd=self.repo,
                                       text=True).strip()

    def write(self, name, text):
        path = self.repo / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def commit(self, message):
        self.git('add', '.')
        self.git('commit', '-q', '-m', message)
        return self.git('rev-parse', 'HEAD')

    def detect(self):
        return subprocess.run(['python3', str(DETECT), self.base, 'HEAD'],
                              cwd=self.repo, text=True, capture_output=True)

    def run_test_at(self, ref):
        script = self.git('show', f'{ref}:{TEST}')
        helper = self.git('show', f'{ref}:helper.sh')
        self.write('probe/helper.sh', helper)
        return subprocess.run(['sh', '-c', script], cwd=self.repo / 'probe',
                              capture_output=True).returncode

    def test_default_regression_still_fails_before_fix(self):
        self.write(TEST, 'sh helper.sh\n')
        test = self.commit('Add a failing regression')
        self.write('helper.sh', 'exit 0\n')
        head = self.commit('Fix the implementation')
        result = self.detect()
        self.assertEqual(result.returncode, 0, result.stderr)
        jobs = list(json.loads(result.stdout)['jobs'].values())
        self.assertEqual([(j['ref'], j['expect']) for j in jobs],
                         [(test, 'fail'), (head, 'pass')])
        self.assertEqual(self.run_test_at(test), 1)
        self.assertEqual(self.run_test_at(head), 0)

    def test_passing_test_is_checked_on_both_commits(self):
        self.write('helper.sh', 'exit 0\n')
        self.commit('Implement the feature')
        self.write(TEST, 'sh helper.sh\n')
        test = self.commit('Add positive coverage\n\nZFS-Test-Expect: pass')
        self.write('unrelated.c', 'int unrelated;\n')
        head = self.commit('Change unrelated code')
        result = self.detect()
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(result.stdout)
        jobs = list(data['jobs'].values())
        self.assertEqual([(j['ref'], j['expect']) for j in jobs],
                         [(test, 'pass'), (head, 'pass')])
        self.assertTrue(all(j['tests'] == TEST.removeprefix('tests/zfs-tests/')
                            for j in jobs))
        self.assertTrue(data['labels'][0].startswith('Passes on test commit:'))
        self.assertEqual(self.run_test_at(test), 0)
        self.assertEqual(self.run_test_at(head), 0)

    def test_invalid_or_duplicate_expectations_are_rejected(self):
        for trailer in ('ZFS-Test-Expect: skip',
                        'ZFS-Test-Expect: pass\nZFS-Test-Expect: fail'):
            with self.subTest(trailer=trailer):
                self.git('reset', '--hard', self.base)
                self.write(TEST, 'sh helper.sh\n')
                self.commit('Add a test\n\n' + trailer)
                self.write('unrelated.c', 'int unrelated;\n')
                self.commit('Change unrelated code')
                result = self.detect()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('ZFS-Test-Expect must appear once',
                              result.stderr)


if __name__ == '__main__':
    unittest.main()
