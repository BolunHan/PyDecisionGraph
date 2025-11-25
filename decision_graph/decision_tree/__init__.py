import logging

from .. import LOGGER

LOGGER = LOGGER.getChild("DecisionTree")

from .exc import *

_USING_CAPI = False
try:
    # Attempt to import the C API module
    from . import capi
    from .capi import *

    _USING_CAPI = True
except Exception:
    # Fallback to the python node model
    from . import native
    from .native import *

    _USING_CAPI = False

from .webui import DecisionTreeWebUi, show, to_html


def set_logger(logger: logging.Logger):
    global LOGGER
    LOGGER = logger

    # ensure abc module (imported above) receives logger
    if _USING_CAPI:
        capi.set_logger(logger.getChild('CAPI'))
    else:
        native.set_logger(logger.getChild('Native'))

    webui.set_logger(logger.getChild('WebUI'))


__all__ = [
    'LOGGER', 'set_logger',
    'Singleton',
    'NodeEdgeCondition', 'ConditionElse', 'ConditionAny', 'ConditionAuto', 'BinaryCondition', 'ConditionTrue', 'ConditionFalse',
    'NO_CONDITION', 'ELSE_CONDITION', 'AUTO_CONDITION', 'TRUE_CONDITION', 'FALSE_CONDITION',
    'SkipContextsBlock', 'LogicExpression', 'LogicNode',
    'LogicGroupManager', 'LGM', 'LogicGroup',
    'ActionNode', 'BreakpointNode', 'PlaceholderNode',
    'NoAction', 'LongAction', 'ShortAction',

    'RootLogicNode', 'ContextLogicExpression',
    'AttrExpression', 'AttrNestedExpression',
    'GetterExpression', 'GetterNestedExpression',
    'MathExpressionOperator', 'MathExpression',
    'ComparisonExpressionOperator', 'ComparisonExpression',
    'LogicalExpressionOperator', 'LogicalExpression',

    'LogicMapping', 'LogicSequence', 'LogicGenerator',

    'DecisionTreeWebUi', 'show', 'to_html'
]
