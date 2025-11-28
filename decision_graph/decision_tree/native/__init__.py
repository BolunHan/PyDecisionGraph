import logging

from .. import LOGGER

LOGGER = LOGGER.getChild('Native')

from .abc import (
    Singleton,
    NodeEdgeCondition, ConditionElse, ConditionAny, ConditionAuto, BinaryCondition, ConditionTrue, ConditionFalse,
    NO_CONDITION, ELSE_CONDITION, AUTO_CONDITION, TRUE_CONDITION, FALSE_CONDITION,
    SkipContextsBlock, LogicExpression, LogicNode,
    LogicGroupManager, LGM, LogicGroup,
    ActionNode, BreakpointNode, PlaceholderNode,
    NoAction, LongAction, ShortAction, CancelAction
)

from .node import (
    RootLogicNode, ContextLogicExpression,
    AttrExpression, AttrNestedExpression,
    GetterExpression, GetterNestedExpression,
    MathExpressionOperator, MathExpression,
    ComparisonExpressionOperator, ComparisonExpression,
    LogicalExpressionOperator, LogicalExpression,
)

from .collection import (
    LogicMapping,
    LogicSequence,
    LogicGenerator,
)


def set_logger(logger: logging.Logger):
    global LOGGER
    LOGGER = logger
    abc.LOGGER = logger.getChild('abc')


__all__ = [
    'LOGGER', 'set_logger',
    'Singleton',
    'NodeEdgeCondition', 'ConditionElse', 'ConditionAny', 'ConditionAuto', 'BinaryCondition', 'ConditionTrue', 'ConditionFalse',
    'NO_CONDITION', 'ELSE_CONDITION', 'AUTO_CONDITION', 'TRUE_CONDITION', 'FALSE_CONDITION',
    'SkipContextsBlock', 'LogicExpression', 'LogicNode',
    'LogicGroupManager', 'LGM', 'LogicGroup',
    'ActionNode', 'BreakpointNode', 'PlaceholderNode',
    'NoAction', 'LongAction', 'ShortAction', 'CancelAction',

    'RootLogicNode', 'ContextLogicExpression',
    'AttrExpression', 'AttrNestedExpression',
    'GetterExpression', 'GetterNestedExpression',
    'MathExpressionOperator', 'MathExpression',
    'ComparisonExpressionOperator', 'ComparisonExpression',
    'LogicalExpressionOperator', 'LogicalExpression',

    'LogicMapping', 'LogicSequence', 'LogicGenerator'
]
