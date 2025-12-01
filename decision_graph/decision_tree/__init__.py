import logging

from .. import LOGGER

LOGGER = LOGGER.getChild("DecisionTree")

from .exc import *

USING_CAPI = False
try:
    # Attempt to import the C API module
    from . import capi
    from .capi import c_abc
    from .capi import c_node
    from .capi import c_collection

    USING_CAPI = True
except Exception:
    # Fallback to the python node model
    from . import native
    from .native import abc
    from .native import node
    from .native import collection

    USING_CAPI = False

if not USING_CAPI:
    from .native import *
else:
    from .capi import *

from .webui import DecisionTreeWebUi, show, to_html


def set_logger(logger: logging.Logger):
    global LOGGER
    LOGGER = logger

    # ensure abc module (imported above) receives logger
    if USING_CAPI:
        capi.set_logger(logger.getChild('CAPI'))
    else:
        native.set_logger(logger.getChild('Native'))

    webui.set_logger(logger.getChild('WebUI'))


__all__ = [
    'USING_CAPI',

    # .exc
    'NO_DEFAULT',
    'EmptyBlock', 'BreakBlock',
    'NodeError', 'TooManyChildren', 'TooFewChildren', 'NodeNotFountError', 'NodeValueError', 'NodeTypeError', 'NodeContextError',
    'EdgeValueError',
    'ResolutionError', 'ExpressFalse', 'ExpressEvaluationError', 'ContextsNotFound',

    # .capi.c_abc or .native.abc
    'LOGGER', 'set_logger',
    'Singleton',
    'NodeEdgeCondition', 'ConditionElse', 'ConditionAny', 'ConditionAuto', 'BinaryCondition', 'ConditionTrue', 'ConditionFalse',
    'NO_CONDITION', 'ELSE_CONDITION', 'AUTO_CONDITION', 'TRUE_CONDITION', 'FALSE_CONDITION',
    'SkipContextsBlock', 'LogicExpression', 'LogicNode',
    'LogicGroupManager', 'LGM', 'LogicGroup',
    'ActionNode', 'BreakpointNode', 'PlaceholderNode',
    'NoAction', 'LongAction', 'ShortAction',

    # .capi.c_node or .native.node
    'RootLogicNode', 'ContextLogicExpression',
    'AttrExpression', 'AttrNestedExpression',
    'GetterExpression', 'GetterNestedExpression',
    'MathExpressionOperator', 'MathExpression',
    'ComparisonExpressionOperator', 'ComparisonExpression',
    'LogicalExpressionOperator', 'LogicalExpression',

    # .capi.c_collection or .native.collection
    'LogicMapping', 'LogicSequence', 'LogicGenerator',

    # .webui
    'DecisionTreeWebUi', 'show', 'to_html'
]
