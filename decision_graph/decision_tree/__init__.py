import logging

from .. import LOGGER

__all__ = ['LOGGER', 'NODE_MODEL',
           'NodeError', 'TooManyChildren', 'TooFewChildren', 'NodeNotFountError', 'NodeValueError', 'EdgeValueError', 'ResolutionError', 'ExpressFalse', 'ContextsNotFound',
           'LGM', 'LogicGroup', 'SkipContextsBlock', 'LogicExpression', 'ExpressionCollection', 'LogicNode', 'ActionNode', 'ELSE_CONDITION',
           'NoAction', 'LongAction', 'ShortAction', 'RootLogicNode', 'ContextLogicExpression', 'AttrExpression', 'MathExpression', 'ComparisonExpression', 'LogicalExpression',
           'LogicMapping', 'LogicGenerator',
           'SignalLogicGroup', 'InstantConfirmationLogicGroup', 'RequestAction', 'PendingRequest', 'DelayedConfirmationLogicGroup', 'RacingConfirmationLogicGroup', 'BarrierConfirmationLogicGroup'
           ]


def set_logger(logger: logging.Logger):
    global LOGGER
    LOGGER = logger

    exc.LOGGER = logger.getChild('TradeUtils')
    abc.LOGGER = logger.getChild('TA')


NODE_MODEL = True

from .exc import *
from .abc import *
from .node import *

if not NODE_MODEL:
    from .expression import *

from .collection import *
from .logic_group import *
