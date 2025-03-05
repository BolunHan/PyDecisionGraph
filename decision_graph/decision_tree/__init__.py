import logging

from .. import LOGGER

__all__ = [
    'LOGGER', 'set_logger', 'activate_expression_model', 'activate_node_model',
    'NodeError', 'TooManyChildren', 'TooFewChildren', 'NodeNotFountError', 'NodeValueError', 'EdgeValueError', 'ResolutionError', 'ExpressFalse', 'ContextsNotFound',
    'LGM', 'LogicGroup', 'SkipContextsBlock', 'LogicExpression', 'ExpressionCollection', 'LogicNode', 'ActionNode', 'ELSE_CONDITION',
    'NoAction', 'LongAction', 'ShortAction', 'RootLogicNode', 'ContextLogicExpression', 'AttrExpression', 'MathExpression', 'ComparisonExpression', 'LogicalExpression',
    'LogicMapping', 'LogicGenerator',
    'SignalLogicGroup', 'InstantConfirmationLogicGroup', 'RequestAction', 'PendingRequest', 'DelayedConfirmationLogicGroup', 'RacingConfirmationLogicGroup', 'BarrierConfirmationLogicGroup'
]

from .exc import *
from .abc import *
from .node import *
from .collection import *
from .logic_group import *


def set_logger(logger: logging.Logger):
    global LOGGER
    LOGGER = logger

    exc.LOGGER = logger.getChild('TradeUtils')
    abc.LOGGER = logger.getChild('TA')


def activate_expression_model():
    import importlib
    importlib.import_module('decision_graph.decision_tree.expression')
    importlib.reload(collection)
    collection.LogicMapping.AttrExpression = AttrExpression
    collection.LogicGenerator.AttrExpression = AttrExpression


def activate_node_model():
    import importlib

    importlib.import_module('decision_graph.decision_tree.node')
    importlib.reload(collection)
    collection.LogicMapping.AttrExpression = AttrExpression
    collection.LogicGenerator.AttrExpression = AttrExpression
