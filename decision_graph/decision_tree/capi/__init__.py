import logging

from .. import LOGGER

LOGGER = LOGGER.getChild('CAPI')

from ..exc import (
    NodeError, TooManyChildren, TooFewChildren, NodeNotFountError, NodeValueError,
    EdgeValueError, ResolutionError, ExpressFalse, ContextsNotFound
)

# import the module so we can set its LOGGER attribute
from . import c_abc

from .c_abc import (
    Singleton,
    NodeEdgeCondition, ConditionElse, ConditionAny, ConditionAuto, BinaryCondition, ConditionTrue, ConditionFalse,
    NO_CONDITION, ELSE_CONDITION, AUTO_CONDITION, TRUE_CONDITION, FALSE_CONDITION,
    SkipContextsBlock, LogicExpression, LogicNode,
    LogicGroupManager, LGM, LogicGroup,
    ActionNode, BreakpointNode, PlaceholderNode,
    NoAction, LongAction, ShortAction,
)

from .c_node import (
    RootLogicNode, ContextLogicExpression,
    AttrExpression, AttrNestedExpression,
    MathExpressionOperator, MathExpression,
    ComparisonExpressionOperator, ComparisonExpression,
    LogicalExpressionOperator, LogicalExpression,
)

from .c_collection import (
    LogicMapping,
    LogicSequence,
    LogicGenerator,
)

__all__ = [
    'LOGGER', 'set_logger',
    'NodeError', 'TooManyChildren', 'TooFewChildren', 'NodeNotFountError', 'NodeValueError', 'EdgeValueError', 'ResolutionError', 'ExpressFalse', 'ContextsNotFound',
    'LGM', 'LogicGroup', 'SkipContextsBlock', 'LogicExpression', 'LogicNode', 'ActionNode', 'ELSE_CONDITION',
    'NoAction', 'LongAction', 'ShortAction', 'RootLogicNode', 'ContextLogicExpression', 'AttrExpression', 'MathExpression', 'ComparisonExpression', 'LogicalExpression',
    'LogicMapping', 'LogicGenerator'
]


def set_logger(logger: logging.Logger):
    global LOGGER
    LOGGER = logger

    c_abc.LOGGER = logger.getChild('abc')
