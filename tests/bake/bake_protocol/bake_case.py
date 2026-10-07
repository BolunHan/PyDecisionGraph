"""The base every bake-protocol case shares: a store of its own, and a tree.

The suite is split by SUBJECT - one file per group of cases - and this module is
the little they have in common. What it supplies is the two things every case
needs and neither the layer nor unittest gives it:

  - a store of its own, because entry names and store names are process-wide and
    a store name ends up inside every read's display text;
  - the tree the cases bake, built the way a build builds one: a root, a branch
    whose operand is a READ (which is not a child of anything - it is reached
    through the node that reads it), and the actions the branch selects between.

Two traps worth writing down, because this suite is full of nodes:

  - ``==`` on two nodes COMPOSES a comparison, it does not compare wrappers.
    Every identity assertion here is ``assertIs`` / ``assertIsNot``, and every
    value assertion is on a Python value the node produced. The same goes for
    membership: ``assertIn`` compares with ``==``, so a node is ``in`` a list of
    nodes as soon as ONE comparison composes - ask with ``any(x is node for x in
    ...)`` instead.
  - a baked graph refuses structural mutation with ``DCG_ERR_BUSY``, which the
    wrappers raise as ``RuntimeError``. A case that asserts a refusal asserts the
    exception, not a return code.
"""

import itertools
import unittest

from decision_graph.decision_tree.bake.c_action import CancelAction, ClearAction, LongAction
from decision_graph.decision_tree.bake.c_collections import LogicMapping
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode

# Store names are short: they end up in every read's display text.
_store_tag = 'bp'
_store_counter = itertools.count()


def store_name() -> str:
    """A store name no other case in this process has taken."""
    return f'{_store_tag}{next(_store_counter)}'


class SignalTree:
    """The tree the cases bake, and the wrappers they assert against.

    ``root`` takes one branch, and that branch is a comparison whose left operand
    is a read of the store's ``signal`` entry. The read is an OPERAND and not a
    child: what the branch does with it is compare it, and the comparison is what
    the tree branches on. Nothing is evaluated here, and no entry is filled -
    building the tree is all this does.
    """

    def __init__(self, mapping: LogicMapping, name: str = 'Entry') -> None:
        self.mapping = mapping
        with RootLogicNode(name=name) as root:
            with mapping:
                self.signal = mapping['signal']
                self.outer = BinaryExpression(ExpressionOperator.gt, self.signal, ConstantNode(0))
                self.inner = BinaryExpression(ExpressionOperator.gt, self.signal, ConstantNode(1))
                with self.outer:
                    with self.inner:
                        self.long = LongAction()
                        self.cancel = CancelAction()
                    self.flat = ClearAction()
        self.root = root


class BakeCase(unittest.TestCase):
    """A case with a store of its own, the tree it bakes, and the doors."""

    def setUp(self) -> None:
        self.mapping = LogicMapping(name=store_name())

    def read(self, key: str):
        """The read of an entry in this case's store, built and owned by it."""
        with self.mapping:
            return self.mapping[key]

    @staticmethod
    def const(value):
        """A literal node for a Python value."""
        return ConstantNode(value)

    def fill(self, **entries) -> None:
        """Put values in entries, reserving the ones that are new."""
        with self.mapping:
            for key, value in entries.items():
                self.mapping[key] = value

    def tree(self, name: str = 'Entry') -> SignalTree:
        """The signal tree, over this case's store."""
        return SignalTree(self.mapping, name)

    def show(self, label: str, *values) -> None:
        """Print one line of evidence, so a run can be read afterwards."""
        print(f'{label:<28} ' + '  '.join(str(value) for value in values))
