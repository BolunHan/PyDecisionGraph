"""The ``with`` clause builds the graph, and the root is what evaluates it.

A build is written with ``with``: entering a node is opening a scope the branches
go into, and the store is a scope too - a read is built inside the store it reads,
which is what makes it that store's. What comes out of the block is a graph, and
the root is the node that evaluates all of it.

The laziness is the subject of the second half: the graph is a structure of READS,
so building it against an empty store and filling the store afterwards is the
normal order, and every walk reads the values of its moment.
"""

import unittest

from decision_graph.decision_tree.bake.c_action import (
    CancelAction,
    ClearAction,
    LongAction,
    ShortAction,
)
from decision_graph.decision_tree.bake.c_collections import LogicMapping
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode

from eval_case import EvalCase


class TestAWithClauseBuild(EvalCase):
    """A tree built with ``with``, walked by its root."""

    def _signal_tree(self):
        """The shape every deciding case uses:

            root
             +-- signal > 0           [the root's one edge]
                  +-- signal > 1      [true]
                  |    +-- long       [true]
                  |    +-- cancel     [false]
                  +-- flat            [false]

        A signal above 1 reaches the long action, one between 0 and 1 the cancel,
        and anything at or below 0 the flat one.
        """
        with RootLogicNode(name='Entry') as root:
            with self.mapping:
                signal = self.mapping['signal']
                outer = BinaryExpression(ExpressionOperator.gt, signal, ConstantNode(0))
                inner = BinaryExpression(ExpressionOperator.gt, signal, ConstantNode(1))
                with outer:
                    with inner:
                        long_ = LongAction()
                        cancel = CancelAction()
                    flat = ClearAction()
        return root, outer, inner, long_, cancel, flat, signal

    def test_00_the_build_returns_a_graph_the_root_evaluates(self) -> None:
        root, outer, inner, long_, cancel, flat, signal = self._signal_tree()
        self.assertIsNone(root.parent)
        self.assertEqual(root.name, 'Entry')
        self.assertFalse(root.is_leaf)
        self.assertIs(root.children[next(iter(root.children))], outer)  # the root's one arm

        self.fill(signal=2.0)
        self.assertIs(root.eval(), long_)

    def test_01_every_value_takes_its_own_arm(self) -> None:
        root, outer, inner, long_, cancel, flat, signal = self._signal_tree()
        for value, expected in ((2.0, long_), (0.5, cancel), (-1.0, flat)):
            with self.subTest(signal=value):
                self.fill(signal=value)
                self.assertIs(root.eval(), expected)
                self.show(f'signal={value}', '->', type(root.eval()).__name__)

    def test_02_the_walk_reads_the_store_at_the_moment_of_the_walk(self) -> None:
        """The graph is built once; every walk asks the store again."""
        root, outer, inner, long_, cancel, flat, signal = self._signal_tree()
        self.fill(signal=2.0)
        self.assertIs(root.eval(), long_)
        self.assertIs(root.eval(), long_)  # a second walk reaches the same leaf

        self.fill(signal=-5.0)
        self.assertIs(root.eval(), flat)  # and the third reads the new value

    def test_03_the_branches_are_the_nodes_the_build_made(self) -> None:
        """The operands of a branch are the very nodes the build wrote."""
        root, outer, inner, long_, cancel, flat, signal = self._signal_tree()
        self.assertIs(outer.operands[0], signal)  # the read this build made
        self.assertEqual(outer.op, ExpressionOperator.gt)
        self.assertEqual(inner.op, ExpressionOperator.gt)
        self.assertEqual(outer.type, 'BINARY')


class TestLaziness(EvalCase):
    """The graph first, the values afterwards."""

    def test_00_a_graph_built_over_an_empty_store_decides_when_filled(self) -> None:
        with RootLogicNode(name='Late') as root:
            with self.mapping:
                read = self.mapping['threshold']
                gate = read > ConstantNode(10)
                with gate:
                    LongAction()
                    ShortAction()

        with self.assertRaises(RuntimeError) as caught:
            root.eval()
        self.assertIn('UNBOUND', str(caught.exception))  # nothing to read yet

        self.fill(threshold=20)
        self.assertEqual(root.eval().type, 'LONGACTION')
        self.fill(threshold=1)
        self.assertEqual(root.eval().type, 'SHORTACTION')

    def test_01_a_read_built_before_the_entry_existed(self) -> None:
        read = self.read('new_entry')
        self.assertTrue(read.out.is_null)
        self.fill(new_entry=42)
        self.assertEqual(read.eval(), 42)

    def test_02_the_same_read_is_reused_by_every_arm_it_reaches(self) -> None:
        """One read, two branches: the value of the moment decides, either way."""
        with RootLogicNode(name='Reused') as root:
            with self.mapping:
                read = self.mapping['k']
                with read > ConstantNode(0):
                    with read > ConstantNode(100):
                        LongAction()
                        CancelAction()
                    ClearAction()

        self.fill(k=200)
        self.assertEqual(root.eval().type, 'LONGACTION')  # both branches took the read
        self.fill(k=1)
        self.assertEqual(root.eval().type, 'CANCELACTION')  # the outer held, the inner did not
        self.fill(k=-1)
        self.assertEqual(root.eval().type, 'CLEARACTION')
        self.assertEqual(read.eval(), -1)  # the read the graph holds, not a copy


class TestNestedStores(EvalCase):
    """Two stores in one graph, and a root inside a store's scope."""

    def test_00_a_graph_over_two_stores(self) -> None:
        other = LogicMapping(name=f'{self.mapping.name}_b')
        with RootLogicNode(name='Two') as root:
            with self.mapping:
                left = self.mapping['x']
            with other:
                right = other['x']
            with left > right:
                LongAction()
                ShortAction()

        self.fill(x=5)
        other['x'] = 1
        self.assertEqual(root.eval().type, 'LONGACTION')

        other['x'] = 9
        self.assertEqual(root.eval().type, 'SHORTACTION')

    def test_01_the_reads_of_two_stores_stay_apart(self) -> None:
        other = LogicMapping(name=f'{self.mapping.name}_c')
        with self.mapping:
            mine = self.mapping['k']
        with other:
            theirs = other['k']
        self.assertIs(mine.logic_group, self.mapping)
        self.assertIs(theirs.logic_group, other)

        self.fill(k=1)
        other['k'] = 2
        self.assertEqual(mine.eval(), 1)
        self.assertEqual(theirs.eval(), 2)


if __name__ == '__main__':
    unittest.main()
