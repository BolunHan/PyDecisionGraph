"""Pin this repository on ``sys.path`` for every test run.

Without it, ``pytest`` inserts only each test file's own directory, and
``import decision_graph`` then resolves to whatever copy is installed in
site-packages — a released wheel, which is not the code under test. ``python -m
pytest`` happens to be safe because ``-m`` puts the working directory first, but
a bare ``pytest`` is not, and the failure it produces is a false one: the suite
runs against an older release and reports the tree's new modules as missing.
The repository root goes first, always, so the tree is what is tested.
"""

import os
import sys

_REPO_ROOT = os.path.dirname(os.path.abspath(__file__))

if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)
