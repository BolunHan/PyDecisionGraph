import logging

from .. import LOGGER

LOGGER = LOGGER.getChild("LogicGroup")

__all__ = [
    'SignalLogicGroup', 'InstantConfirmationLogicGroup',
    'StateMapping', 'RequestAction', 'PendingRequest', 'RequestConfirmed', 'RequestDenied', 'RequestRegistered', 'DelayedConfirmationLogicGroup',
]


def set_logger(logger: logging.Logger):
    global LOGGER
    LOGGER = logger

    base.LOGGER = logger.getChild('base')
    pending_request.LOGGER = logger.getChild('delayed')


from .base import *
from .pending_request import *
