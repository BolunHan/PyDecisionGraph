import logging

from .. import LOGGER

LOGGER = LOGGER.getChild("LogicGroup")


def set_logger(logger: logging.Logger):
    global LOGGER
    LOGGER = logger

    base.LOGGER = logger.getChild('base')
    pending_request.LOGGER = logger.getChild('delayed')


from .base import *
from .pending_request import *

__all__ = [
    'SignalLogicGroup', 'InstantConfirmationLogicGroup',

    'RequestType', 'RequestStatus', 'PendingRequest',
    'RequestRegistered', 'RequestDenied', 'RequestConfirmed',
    'DelayedConfirmationLogicGroup'
]
