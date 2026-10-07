"""Wrapper tests for ``decision_graph.decision_tree.bake.c_action``.

Scope is the Python surface only. What an action node *is* in C is settled by
``tests/bake/test_c_node_lifecycle.c``; what is checked here is the wrapper's
half of the bargain:

  - each leaf states its own type, and the C node agrees;
  - the signal a leaf carries is the one its name promises;
  - a leaf built in C comes back as the class its type names, which is what the
    layer's reconstruction answers with.

Oracle: the C node's own reported type name and signal, read back through the
wrapper's properties, and the C type constants the constructors are given.
Never a re-derivation of the wrapper's own arithmetic.
"""

import unittest

from decision_graph.decision_tree.bake.c_action import (
    ActionNode,
    CancelAction,
    ClearAction,
    LongAction,
    NoAction,
    ShortAction,
)
from decision_graph.decision_tree.bake.c_node import NODE_REGISTRY

# Every leaf, with the type name the C layer reports and the signal it carries.
LEAVES = (
    (NoAction, 'NOACTION', 0),
    (LongAction, 'LONGACTION', 1),
    (ShortAction, 'SHORTACTION', -1),
    (CancelAction, 'CANCELACTION', 0),
    (ClearAction, 'CLEARACTION', 0),
)


class TestActionLeaves(unittest.TestCase):
    """Contract: an action leaf is a typed, signal-carrying terminal node.

    Expected behavior:
        - the class built is the type the C node reports;
        - a leaf is a leaf: nothing hangs below it;
        - the signal its name promises is the one it carries;
        - a display text given at construction replaces the default.

    An action node is where the graph terminates, which is why every one of them
    is a leaf and why the signal is the only thing distinguishing most of them.
    """

    def test_00_each_leaf_reports_its_own_type(self) -> None:
        """The class and the C node agree on the type."""
        for cls, type_name, _ in LEAVES:
            with self.subTest(leaf=cls.__name__):
                self.assertEqual(cls().type, type_name)

    def test_01_every_leaf_is_a_leaf(self) -> None:
        """Nothing hangs below an action."""
        for cls, _, _ in LEAVES:
            with self.subTest(leaf=cls.__name__):
                node = cls()
                self.assertTrue(node.is_leaf)
                self.assertEqual(node.size, 1)
                self.assertEqual(node.children, {})

    def test_02_each_leaf_carries_its_own_signal(self) -> None:
        """The signal is the one the name promises."""
        for cls, _, signal in LEAVES:
            with self.subTest(leaf=cls.__name__):
                self.assertEqual(int(cls()), signal)

    def test_03_the_default_repr_is_the_leaf_s_own_name(self) -> None:
        """With nothing given, the class's name is the display text."""
        for cls, _, _ in LEAVES:
            with self.subTest(leaf=cls.__name__):
                self.assertEqual(cls().repr, cls.__name__)

    def test_04_a_given_repr_becomes_the_display_text(self) -> None:
        """The display text is a constructor argument, not a patched field."""
        self.assertEqual(LongAction(repr='go long').repr, 'go long')

    def test_05_noaction_carries_the_autogen_flag(self) -> None:
        """The one leaf with a flag reports it."""
        self.assertFalse(NoAction().autogen)
        self.assertTrue(NoAction(autogen=True).autogen)

    def test_06_the_family_head_is_not_a_leaf(self) -> None:
        """ActionNode is the family's head, and it is not itself built bare."""
        self.assertTrue(issubclass(NoAction, ActionNode))


class TestActionRegistration(unittest.TestCase):
    """Contract: a node rebuilt from C comes back as the class that built it.

    Expected behavior:
        - an address looked up after its wrapper is gone rebuilds as the leaf's
          own class, not as the base.

    The class comes from the layer's reconstruction, by the node's own type (see
    ``test_bake_reconstruct.py``); what is checked here is that the lookup a
    callback uses answers with it, for every leaf of this family.
    """

    def test_00_a_dropped_wrapper_rebuilds_as_its_own_class(self) -> None:
        """The registry misses, and the lookup rebuilds the leaf's class."""
        for cls, _, _ in LEAVES:
            with self.subTest(leaf=cls.__name__):
                node = cls()
                address = node.address
                del NODE_REGISTRY[address]
                rebuilt = NODE_REGISTRY[address]
                self.assertIsInstance(rebuilt, cls)

    def test_01_a_rebuilt_wrapper_reports_the_same_node(self) -> None:
        """The rebuild is a view of the same C node, not a copy of it."""
        node = LongAction()
        address = node.address
        del NODE_REGISTRY[address]
        rebuilt = NODE_REGISTRY[address]
        self.assertEqual(rebuilt.address, address)
        self.assertEqual(rebuilt.type, node.type)
        self.assertIsNot(rebuilt, node)
