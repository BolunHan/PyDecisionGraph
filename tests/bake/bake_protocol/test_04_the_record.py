"""The record: the room a walk needs, made before the walk needs it.

A walk records what it visits into the root's own record, and a record with no
room takes room as it goes - an allocation inside the walk. That is the only
allocation an evaluation makes, and removing it is what "prepared to be walked"
buys: the room is made at bake time, sized for what a walk from this root can
ever use - one entry per level, and no walk goes deeper than the graph.

These cases measure that from the outside, because the allocation is not
something a caller can see: the room before and after a bake, that a walk of a
baked graph leaves it exactly as it was, and that a graph walked before it was
baked is already prepared.
"""

import unittest
from contextlib import ExitStack

from decision_graph.decision_tree.bake.c_action import LongAction
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode

from bake_case import BakeCase


class TestTheRecord(BakeCase):
    """The record's room, at bake time and after it."""

    def test_00_a_graph_that_was_never_walked_has_no_record(self) -> None:
        tree = self.tree()

        self.assertEqual(len(tree.root.eval_path), 0)
        self.assertEqual(tree.root.eval_path.capacity, 0)  # nothing has been walked

    def test_01_a_bake_makes_the_room_a_walk_can_need(self) -> None:
        tree = self.tree()

        report = tree.root.bake()
        # One entry per level: the root, the two branches below it and the leaf.
        self.assertEqual(report.capacity, 4)
        self.assertEqual(tree.root.eval_path.capacity, 4)
        self.assertEqual(len(tree.root.eval_path), 0)  # made, and not yet used

    def test_02_a_walk_of_a_baked_graph_grows_nothing(self) -> None:
        tree = self.tree()
        tree.root.bake()
        capacity = tree.root.eval_path.capacity

        self.fill(signal=2.0)
        self.assertIs(tree.root.eval(), tree.long)
        self.assertEqual(tree.root.eval_path.capacity, capacity)

        # Again, with a deeper walk: the room is what a walk can need, so no walk
        # needs more.
        self.fill(signal=0.5)
        self.assertIs(tree.root.eval(), tree.cancel)
        self.assertEqual(tree.root.eval_path.capacity, capacity)
        self.assertEqual(len(tree.root.eval_path), 4)  # the whole of this one

    def test_03_a_graph_walked_before_it_was_baked_is_prepared_already(self) -> None:
        tree = self.tree()
        self.fill(signal=2.0)
        self.assertIs(tree.root.eval(), tree.long)  # the walk made its own room

        report = tree.root.bake()
        self.assertEqual(report.capacity, tree.root.eval_path.capacity)
        self.assertEqual(report.capacity, 4)

    def test_04_a_baked_graph_still_records_what_it_decided(self) -> None:
        tree = self.tree()
        tree.root.bake()

        self.fill(signal=2.0)
        tree.root.eval()

        path = tree.root.eval_path
        self.assertEqual(path.code_name, 'OK')
        self.assertIs(path.leaf, tree.long)
        self.assertEqual(len(path), 4)
        self.assertIs(path[0], tree.root)
        self.assertIs(path[-1], tree.long)

    def test_05_the_room_follows_the_graph_not_a_guess(self) -> None:
        levels = 6
        with RootLogicNode(name='Deep') as root:
            with self.mapping:
                signal = self.mapping['signal']
                with ExitStack() as stack:
                    for level in range(levels):
                        stack.enter_context(BinaryExpression(ExpressionOperator.gt, signal, ConstantNode(levels - level)))
                    LongAction()

        report = root.bake()
        # The root, one comparison per level, and the action at the bottom.
        self.assertEqual(report.depth, levels + 1)
        self.assertEqual(report.capacity, levels + 2)
        self.assertEqual(root.eval_path.capacity, levels + 2)

        self.fill(signal=levels + 1)
        self.assertEqual(root.eval_path.capacity, levels + 2)  # and the walk agrees
        self.assertEqual(root.eval_path.code_name, 'OK')


if __name__ == '__main__':
    unittest.main()
