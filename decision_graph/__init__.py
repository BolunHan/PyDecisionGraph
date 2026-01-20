__version__ = "0.2.9"

import functools
import logging
import os
import sys

LOGGER = logging.getLogger("DecisionGraph")
LOGGER.setLevel(logging.INFO)

if not LOGGER.hasHandlers():
    ch = logging.StreamHandler(sys.stdout)
    ch.setLevel(logging.INFO)  # Set handler level
    formatter = logging.Formatter("%(asctime)s - %(levelname)s - %(message)s")
    ch.setFormatter(formatter)
    LOGGER.addHandler(ch)

from . import decision_tree
from . import logic_group


@functools.cache
def get_include():
    res_dir = os.path.dirname(__file__)
    LOGGER.info(f'Building with <PyDecisionGraph> version: "{__version__}", resource directory: "{res_dir}".')
    return res_dir
