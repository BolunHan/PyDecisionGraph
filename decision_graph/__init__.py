__version__ = "0.1.2"

import logging
import sys

LOGGER = logging.getLogger("DecisionGraph")
LOGGER.setLevel(logging.INFO)

if not LOGGER.hasHandlers():
    ch = logging.StreamHandler(sys.stdout)
    ch.setLevel(logging.INFO)  # Set handler level
    formatter = logging.Formatter("%(asctime)s - %(levelname)s - %(message)s")
    ch.setFormatter(formatter)
    LOGGER.addHandler(ch)


