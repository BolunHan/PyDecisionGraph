import logging

from .. import LOGGER

LOGGER = LOGGER.getChild("DecisionTree")

__all__ = [
    'LOGGER', 'set_logger', 'activate_expression_model', 'activate_node_model',
    'NodeError', 'TooManyChildren', 'TooFewChildren', 'NodeNotFountError', 'NodeValueError', 'EdgeValueError', 'ResolutionError', 'ExpressFalse', 'ContextsNotFound',
    'LGM', 'LogicGroup', 'SkipContextsBlock', 'LogicExpression', 'ExpressionCollection', 'LogicNode', 'ActionNode', 'ELSE_CONDITION',
    'NoAction', 'LongAction', 'ShortAction', 'RootLogicNode', 'ContextLogicExpression', 'AttrExpression', 'MathExpression', 'ComparisonExpression', 'LogicalExpression',
    'LogicMapping', 'LogicGenerator'
]

from .exc import *
from .abc import *
from .node import *
from .collection import *


def set_logger(logger: logging.Logger):
    global LOGGER
    LOGGER = logger

    abc.LOGGER = logger.getChild('abc')


def activate_expression_model():
    import importlib
    importlib.import_module('decision_graph.decision_tree.expression')
    importlib.reload(collection)
    collection.LogicMapping.AttrExpression = AttrExpression
    collection.LogicGenerator.AttrExpression = AttrExpression
    # importlib.reload(logic_group)


def activate_node_model():
    import importlib

    importlib.import_module('decision_graph.decision_tree.node')
    importlib.reload(collection)
    collection.LogicMapping.AttrExpression = AttrExpression
    collection.LogicGenerator.AttrExpression = AttrExpression
    # importlib.reload(logic_group)
