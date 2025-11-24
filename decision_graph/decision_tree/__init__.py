import logging

from .. import LOGGER

LOGGER = LOGGER.getChild("DecisionTree")

__all__ = [
    'LOGGER', 'set_logger',
    'NodeError', 'TooManyChildren', 'TooFewChildren', 'NodeNotFountError', 'NodeValueError', 'EdgeValueError', 'ResolutionError', 'ExpressFalse', 'ContextsNotFound',
    'LGM', 'LogicGroup', 'SkipContextsBlock', 'LogicExpression', 'LogicNode', 'ActionNode', 'ELSE_CONDITION',
    'NoAction', 'LongAction', 'ShortAction', 'RootLogicNode', 'ContextLogicExpression', 'AttrExpression', 'MathExpression', 'ComparisonExpression', 'LogicalExpression',
    'LogicMapping', 'LogicGenerator'
]

from .exc import *

_USING_CAPI = False
try:
    # Attempt to import the C API module
    from . import capi
    from .capi.c_abc import *
    from .capi.c_node import *
    from .capi.c_collection import *

    _USING_CAPI = True
except Exception:
    # Fallback to the python node model
    from . import native
    from .native.abc import *
    from .native.node import *
    from .native.collection import *

    _USING_CAPI = False


def set_logger(logger: logging.Logger):
    global LOGGER
    LOGGER = logger

    # ensure abc module (imported above) receives logger
    if _USING_CAPI:
        capi.set_logger(logger.getChild('CAPI'))
    else:
        native.set_logger(logger.getChild('Native'))
