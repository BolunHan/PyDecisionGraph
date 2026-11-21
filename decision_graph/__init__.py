__version__ = "0.3.5"

import functools
import logging
import os
import pathlib

from cbase.backports.telemetrics import get_logger as _get_logger


def get_logger(**kwargs) -> logging.Logger:
    return _get_logger(name='DecisionGraph', **kwargs)


LOGGER = get_logger()

from . import decision_tree as decision_tree
from . import logic_group as logic_group


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
