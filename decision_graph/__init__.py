__version__ = "0.3.3"

import functools
import logging
import os
import pathlib
import sys

LOGGER = logging.getLogger("DecisionGraph")
LOGGER.setLevel(logging.INFO)

if not LOGGER.hasHandlers():
    ch = logging.StreamHandler(sys.stdout)
    ch.setLevel(logging.INFO)  # Set handler level
    formatter = logging.Formatter("%(asctime)s - %(levelname)s - %(message)s")
    ch.setFormatter(formatter)
    LOGGER.addHandler(ch)

from . import decision_tree as decision_tree  # noqa: E402
from . import logic_group as logic_group  # noqa: E402


@functools.cache
def get_include() -> list[str]:
    res_dir = pathlib.Path(__file__).parent
    LOGGER.info(f'Building with <PyDecisionGraph> version: "{__version__}", resource directory: "{res_dir}".')

    src_dirs = [
        os.path.realpath(res_dir),
        os.path.realpath(res_dir / 'decision_tree'),
        os.path.realpath(res_dir / 'decision_tree' / 'bake'),
        os.path.realpath(res_dir / 'decision_tree' / 'capi'),
    ]

    include_root = os.path.realpath(res_dir / 'includes')
    if os.path.isdir(include_root):
        src_dirs.append(include_root)

    return src_dirs
