"""The parity suite's door into ``pytest``.

``tests/bake_parity/`` runs each arm of a case in a process of its own and
compares the transcripts they leave behind - which is the point of the design,
and also why it cannot be a normal test module: the comparison needs the arms to
have FINISHED, and the artifacts to exist on disk before anything is read.

So the suite has one entry point, and this is it. Running this file runs the
parity runner exactly as a caller would, over every case it knows, and reports
its exit status. A case that disagrees with itself therefore fails the ordinary
``pytest tests/`` run without the suite having to know how the comparison works.

The runner's own output is captured rather than printed: when a case fails, its
detail belongs in the artifact under ``tests/bake_parity/artifacts/``, where it
can be read alongside the transcripts it is about.
"""

import os
import subprocess
import sys
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNNER = os.path.join(REPO_ROOT, 'tests', 'bake_parity', 'runner.py')


class TestTheParitySuiteAgrees(unittest.TestCase):
    """Contract: every case's three arms answer the same way.

    Expected behavior:
        - the runner exits zero, which it only does when every arm ran to the
          end, every arm matched the design's expected leaf, and every arm's
          comparable transcript matched the others';
        - a failure quotes the runner's own report, so the reason is in the
          test output as well as in the artifacts.
    """

    def test_000_every_case_agrees_across_its_arms(self) -> None:
        """Run the parity runner over every case and require its success."""
        result = subprocess.run(
            [sys.executable, RUNNER],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            timeout=900,
        )
        self.assertEqual(result.returncode, 0, f'{result.stdout}\n--- stderr ---\n{result.stderr}')
