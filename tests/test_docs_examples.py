"""The documentation's examples, executed.

Every script under ``docs/examples/`` is run in a subprocess, from a temporary
directory, with this repository on the import path. A page that quotes an
example quotes something that runs; a change that breaks one breaks this suite
first.
"""

import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
EXAMPLES_DIR = REPO_ROOT / 'docs' / 'examples'

EXAMPLES = sorted(p.name for p in EXAMPLES_DIR.glob('*.py'))


class TestTheDocumentationExamples(unittest.TestCase):
    """Contract: every documented example runs to a clean exit."""

    def test_00_there_are_examples_to_run(self) -> None:
        """A silent rename would otherwise turn this suite into a no-op."""
        self.assertGreaterEqual(len(EXAMPLES), 5)

    def test_01_every_example_exits_zero(self) -> None:
        """Run each example from a scratch directory and require a clean exit."""
        for name in EXAMPLES:
            with self.subTest(example=name):
                env = dict(os.environ)
                env['PYTHONPATH'] = str(REPO_ROOT)
                with tempfile.TemporaryDirectory() as cwd:
                    result = subprocess.run(
                        [sys.executable, str(EXAMPLES_DIR / name)],
                        cwd=cwd,
                        env=env,
                        capture_output=True,
                        text=True,
                        timeout=120,
                    )
                self.assertEqual(result.returncode, 0, f'{name}:\n{result.stdout}\n{result.stderr}')
