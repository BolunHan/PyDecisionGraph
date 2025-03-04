__version__ = "0.1.0"

import logging
import sys

LOGGER = logging.getLogger("DecisionTree")
LOGGER.setLevel(logging.INFO)

if not LOGGER.hasHandlers():
    ch = logging.StreamHandler(sys.stdout)
    ch.setLevel(logging.INFO)  # Set handler level
    formatter = logging.Formatter("%(asctime)s - %(levelname)s - %(message)s")
    ch.setFormatter(formatter)
    LOGGER.addHandler(ch)


def set_logger(logger: logging.Logger):
    global LOGGER
    LOGGER = logger

    exc.LOGGER = logger.getChild('TradeUtils')
    abc.LOGGER = logger.getChild('TA')


NODE_MODEL = True

from .exc import *
from .abc import *

if NODE_MODEL:
    from .node import *
else:
    from .expression import *

from .collection import *
from .logic_group import *
