import logging

from .. import LOGGER

LOGGER = LOGGER.getChild('Native')

# NOTE: Default model
# The decision tree package exposes expression classes (e.g. AttrExpression) at the
# package level. The import order below determines which implementation is the
# "default" when the package is imported. Currently, we import the node-based
# models before the collection module, so the AttrExpression exported by the
# package is the node model implementation by default (i.e. defined in
# `decision_tree.node`). That means, unless you explicitly switch modes, the
# node model is active.
#
# The provided helper functions `activate_expression_model()` and
# `activate_node_model()` are intended to switch which AttrExpression the
# collection classes (LogicMapping / LogicGenerator) use. The implementations
# below will import the chosen module (node or expression), set the package
# AttrExpression to the chosen implementation, and then reload the `collection`
# module so it binds the appropriate AttrExpression class.

__all__ = [
    'LOGGER', 'set_logger', 'activate_expression_model', 'activate_node_model',
    'NodeError', 'TooManyChildren', 'TooFewChildren', 'NodeNotFountError', 'NodeValueError', 'EdgeValueError', 'ResolutionError', 'ExpressFalse', 'ContextsNotFound',
    'LGM', 'LogicGroup', 'SkipContextsBlock', 'LogicExpression', 'ExpressionCollection', 'LogicNode', 'ActionNode', 'ELSE_CONDITION',
    'NoAction', 'LongAction', 'ShortAction', 'RootLogicNode', 'ContextLogicExpression', 'AttrExpression', 'MathExpression', 'ComparisonExpression', 'LogicalExpression',
    'LogicMapping', 'LogicGenerator'
]

from ..exc import *
from .abc import *
from .node import *
from .collection import *


def set_logger(logger: logging.Logger):
    global LOGGER
    LOGGER = logger

    abc.LOGGER = logger.getChild('abc')


def activate_expression_model():
    """Switch to the expression-model implementations.

    This imports `decision_graph.decision_tree.expression`, sets the package's
    `AttrExpression` to the expression implementation, reloads `collection`,
    and updates `collection` bindings so LogicMapping/LogicGenerator will use
    the expression AttrExpression.
    """
    import importlib

    # Import the expression module and pick its AttrExpression
    expr_mod = importlib.import_module('decision_graph.decision_tree.expression')

    # Set the package-level AttrExpression to the one defined in expression.py
    # so `from decision_graph.decision_tree import AttrExpression` returns the
    # expression-model version.
    global AttrExpression, ContextLogicExpression, MathExpression, ComparisonExpression, LogicalExpression
    AttrExpression = expr_mod.AttrExpression
    ContextLogicExpression = expr_mod.ContextLogicExpression
    MathExpression = expr_mod.MathExpression
    ComparisonExpression = expr_mod.ComparisonExpression
    LogicalExpression = expr_mod.LogicalExpression

    # Reload collection so it picks up the new AttrExpression binding from the
    # package and then explicitly patch the classes to be safe.
    importlib.reload(importlib.import_module('decision_graph.decision_tree.collection'))
    import decision_graph.decision_tree.native.collection as dt_collection
    dt_collection.LogicMapping.AttrExpression = AttrExpression
    dt_collection.LogicGenerator.AttrExpression = AttrExpression


def activate_node_model():
    """Switch to the node-model implementations.

    This imports `decision_graph.decision_tree.node`, sets the package's
    `AttrExpression` to the node implementation, reloads `collection`, and
    updates `collection` bindings so LogicMapping/LogicGenerator will use the
    node AttrExpression.
    """
    import importlib

    node_mod = importlib.import_module('decision_graph.decision_tree.node')

    global AttrExpression, ContextLogicExpression, MathExpression, ComparisonExpression, LogicalExpression
    AttrExpression = node_mod.AttrExpression
    ContextLogicExpression = node_mod.ContextLogicExpression
    MathExpression = node_mod.MathExpression
    ComparisonExpression = node_mod.ComparisonExpression
    LogicalExpression = node_mod.LogicalExpression

    importlib.reload(importlib.import_module('decision_graph.decision_tree.collection'))
    import decision_graph.decision_tree.native.collection as dt_collection
    dt_collection.LogicMapping.AttrExpression = AttrExpression
    dt_collection.LogicGenerator.AttrExpression = AttrExpression
    # importlib.reload(logic_group)
