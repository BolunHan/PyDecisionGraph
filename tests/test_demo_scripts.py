"""The three protocol demos, executed.

One demo per layer - native, capi, bake - under ``demo/``. Each builds the same
decision, prints its session, and writes its layer's HTML page. This suite runs
every one from a scratch directory, so a demo that stops matching the current
tree fails here before the documentation quotes it.
"""

import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
DEMOS_DIR = REPO_ROOT / 'demo'
DEMOS = ('native_demo.py', 'capi_demo.py', 'bake_demo.py')


class TestTheProtocolDemos(unittest.TestCase):
    """Contract: one runnable demo per layer, and every demo runs clean."""

    def test_00_the_three_protocol_demos_exist(self) -> None:
        """A rename cannot silently drop a layer from the set."""
        for name in DEMOS:
            with self.subTest(demo=name):
                self.assertTrue((DEMOS_DIR / name).is_file(), f'missing demo/{name}')

    def test_01_every_demo_runs_and_writes_its_page(self) -> None:
        """Each demo exits zero, prints its session, and writes its HTML file."""
        for name in DEMOS:
            with self.subTest(demo=name):
                env = dict(os.environ)
                env['PYTHONPATH'] = str(REPO_ROOT)
                with tempfile.TemporaryDirectory() as cwd:
                    result = subprocess.run(
                        [sys.executable, str(DEMOS_DIR / name)],
                        cwd=cwd,
                        env=env,
                        capture_output=True,
                        text=True,
                        timeout=120,
                    )
                    self.assertEqual(
                        result.returncode,
                        0,
                        f'{name}:\n{result.stdout}\n{result.stderr}',
                    )
                    self.assertIn('decision:', result.stdout)
                    self.assertIn('walk:', result.stdout)
                    page = pathlib.Path(cwd) / f'{name.removesuffix("_demo.py")}_demo.html'
                    self.assertIn(f'html: {page.name}', result.stdout)
                    self.assertTrue(page.is_file(), f'{name} did not write {page.name}')
                    self.assertGreater(page.stat().st_size, 0)
                    if name == 'bake_demo.py':
                        self.assertIn('bake: OK', result.stdout)
