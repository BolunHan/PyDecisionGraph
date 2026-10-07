"""The record of a walk: it is the walk, in order, and nothing it did not reach.

An evaluation is a decision, and the record is the answer to "how was it
decided": the root first, every branch the walk descended through in between, and
the leaf it came to rest on last - the very wrappers the build made, not fresh
views of them - plus the outcome: the code, the leaf, the failure if there was
one, and the id of the run that wrote it.

What the record proves about the protocol is the shape of the walk:

  - nothing is recorded that was not evaluated, so the record is the shortest
    path from the root to the leaf;
  - it is written by the WALK and by nothing else: a single node's evaluation -
    an eval or a dry run - leaves the last walk's record standing;
  - a walk that fails holds the nodes it got through and no leaf, so "where did
    it stop being reachable" is answerable;
  - the view reads the record live, so it is not a snapshot: the same view answers
    for the walk that is current.
"""

import unittest

from decision_graph.decision_tree.bake.c_action import CancelAction, ClearAction, LongAction
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode

from eval_case import EvalCase


class TestTheRecord(EvalCase):
    """The signal tree, and what its record holds after each walk."""

    def _tree(self):
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
        return root, outer, inner, long_, cancel, flat

    def test_00_the_record_is_the_walk_in_order(self) -> None:
        root, outer, inner, long_, cancel, flat = self._tree()
        self.fill(signal=2.0)
        landed = root.eval()

        path = root.eval_path
        self.assertEqual(len(path), 4)
        self.assertEqual([node.type for node in path], ['ROOT', 'BINARY', 'BINARY', 'LONGACTION'])
        self.assertEqual([node.type for node in path.nodes], ['ROOT', 'BINARY', 'BINARY', 'LONGACTION'])

        self.assertIs(path[0], root)
        self.assertIs(path[1], outer)
        self.assertIs(path[2], inner)
        self.assertIs(path[3], long_)
        self.assertIs(path[3], landed)
        self.assertIs(path[-1], landed)  # counting from the back is the same node
        self.assertIs(path.leaf, landed)
        self.assertEqual(path.code_name, 'OK')
        self.assertIsNone(path.failed)
        self.show('record', ' -> '.join(node.type for node in path))

    def test_01_a_shorter_walk_records_less(self) -> None:
        root, outer, inner, long_, cancel, flat = self._tree()
        self.fill(signal=-1.0)
        landed = root.eval()

        self.assertEqual([node.type for node in root.eval_path], ['ROOT', 'BINARY', 'CLEARACTION'])
        self.assertIs(root.eval_path.leaf, flat)
        self.assertIs(landed, flat)
        # The branch it never descended through is not in the record - asserted by
        # identity, because comparing nodes composes a comparison node.
        self.assertFalse(any(node is inner for node in root.eval_path.nodes))

    def test_02_the_record_is_read_live(self) -> None:
        """One view, taken from the root, answering for whatever walk is current."""
        root, outer, inner, long_, cancel, flat = self._tree()
        view = root.eval_path

        self.fill(signal=2.0)
        root.eval()
        self.assertIs(view.leaf, long_)
        self.assertEqual(len(view), 4)

        self.fill(signal=0.5)
        root.eval()
        self.assertIs(view.leaf, cancel)  # the same view, the current walk
        self.assertIs(view[2], inner)
        self.assertEqual(len(view), 4)

    def test_03_the_run_id_is_the_walks_and_changes_with_it(self) -> None:
        root, outer, inner, long_, cancel, flat = self._tree()
        self.fill(signal=2.0)

        root.eval()
        first = root.eval_path.seq_id
        self.assertNotEqual(first, 0)  # a walk is a run, so it has one

        root.eval()
        self.assertNotEqual(root.eval_path.seq_id, first)

    def test_04_the_record_of_a_failed_walk(self) -> None:
        """A walk that stopped holds the nodes it reached, and says where it stopped."""
        root, outer, inner, long_, cancel, flat = self._tree()
        with self.assertRaises(RuntimeError) as caught:
            root.eval()
        self.assertIn('UNBOUND', str(caught.exception))

        path = root.eval_path
        self.assertEqual([node.type for node in path], ['ROOT', 'BINARY'])
        self.assertIsNone(path.leaf)  # it never landed
        self.assertIs(path.failed, outer)  # the node whose own evaluation refused
        self.assertEqual(path.code_name, 'UNBOUND')
        self.assertNotEqual(path.seq_id, 0)  # it happened in a run

    def test_05_the_capacity_is_the_room_the_record_has(self) -> None:
        root, outer, inner, long_, cancel, flat = self._tree()
        self.fill(signal=2.0)
        root.eval()
        self.assertGreaterEqual(root.eval_path.capacity, len(root.eval_path))
        self.assertNotEqual(root.eval_path.address, 0)


class TestTheRecordAndSingleNodeEvals(EvalCase):
    """What writes the record, and what does not."""

    def test_00_a_single_node_leaves_the_record_alone(self) -> None:
        root, outer = self._two_level()
        self.fill(signal=2.0)
        root.eval()
        before = (root.eval_path.seq_id, [node.type for node in root.eval_path])

        outer.eval()
        outer.dry_run()
        self.assertEqual((root.eval_path.seq_id, [node.type for node in root.eval_path]), before)
        self.show('after a single-node eval and a dry run', [node.type for node in root.eval_path])

    def test_01_a_walk_writes_it(self) -> None:
        root, outer = self._two_level()
        self.assertEqual(len(root.eval_path), 0)  # nothing has walked yet
        self.fill(signal=2.0)
        root.eval()
        self.assertEqual(len(root.eval_path), 3)

    def _two_level(self):
        with RootLogicNode(name='Entry') as root:
            with self.mapping:
                signal = self.mapping['signal']
                outer = BinaryExpression(ExpressionOperator.gt, signal, ConstantNode(0))
                with outer:
                    LongAction()
                    ClearAction()
        return root, outer


if __name__ == '__main__':
    unittest.main()
