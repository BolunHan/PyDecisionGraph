"""Asking a node in the middle of a graph: it updates, and nothing above it does.

A walk goes from the top down. A single node's ``eval`` goes nowhere: it runs that
node, and that node runs ITS operands - which is downward, never upward. So a
branch that is asked on its own leaves everything above it holding what it last
held, and the calls on the way down are what get new values.

That is what "localized update" means here, and it is what these cases measure:

  - the node asked reports the new value, and so do its operands (they ran);
  - its parent, its siblings and the root keep theirs (they did not);
  - the root's record of its last walk is untouched, because a single node's
    evaluation is not a walk.
"""

import unittest

from decision_graph.decision_tree.bake.c_action import CancelAction, ClearAction, LongAction
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode

from eval_case import EvalCase


class TestMidGraphEval(EvalCase):
    """The signal tree, with one branch asked directly."""

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
        return root, signal, outer, inner, long_, cancel, flat

    def test_00_a_branch_evaluated_alone_updates_nothing_above_it(self) -> None:
        root, signal, outer, inner, long_, cancel, flat = self._tree()
        self.fill(signal=2.0)

        self.assertIs(root.eval(), long_)  # a walk first: everything holds a value
        self.assertIs(outer.out.value, True)
        self.assertIs(inner.out.value, True)

        # The store changes so that a walk would now take another arm...
        self.fill(signal=0.5)
        # ...and only the inner branch is asked. It reports the new answer.
        self.assertIs(inner.eval(), False)

        # Its own slot is what changed; its parent's is what did not.
        self.assertIs(inner.out.value, False)
        self.assertIs(outer.out.value, True)  # stale, and that is the point
        self.assertIsNot(outer.out.value, inner.out.value)
        self.show('inner asked alone', 'inner', inner.out.value, 'outer', outer.out.value)

    def test_01_the_operands_of_the_node_asked_are_run(self) -> None:
        """Asking a branch runs the read under it, and the read is live."""
        root, signal, outer, inner, long_, cancel, flat = self._tree()
        self.fill(signal=2.0)
        root.eval()

        self.fill(signal=0.5)
        self.assertIs(inner.eval(), False)
        self.assertEqual(signal.out.value, 0.5)  # the read ran: it reports the store now
        self.assertIs(signal.out.is_ref, True)

    def test_02_a_sibling_keeps_its_value(self) -> None:
        """The other arm of the same branch was not run, and stands as it stood."""
        root, signal, outer, inner, long_, cancel, flat = self._tree()
        self.fill(signal=2.0)
        self.assertIs(root.eval(), long_)
        before = flat.out.format()

        self.fill(signal=0.5)
        self.assertIs(inner.eval(), False)
        self.assertEqual(flat.out.format(), before)  # the sibling holds what it held
        self.assertIs(flat.eval(), flat)  # and still stands for itself

    def test_03_the_root_record_of_the_last_walk_is_untouched(self) -> None:
        """A single node's evaluation is not a run, so no record is rewritten."""
        root, signal, outer, inner, long_, cancel, flat = self._tree()
        self.fill(signal=2.0)
        root.eval()

        path = root.eval_path
        seq_id = path.seq_id
        nodes = [node.type for node in path.nodes]

        self.fill(signal=0.5)
        inner.eval()
        outer.eval()

        self.assertEqual(root.eval_path.seq_id, seq_id)  # the same run is still recorded
        self.assertEqual([node.type for node in root.eval_path], nodes)
        self.assertEqual(root.eval_path.code_name, 'OK')

    def test_04_asking_the_root_again_is_a_new_walk(self) -> None:
        """The root is the difference: evaluating it replaces the record."""
        root, signal, outer, inner, long_, cancel, flat = self._tree()
        self.fill(signal=2.0)
        root.eval()
        first = root.eval_path.seq_id

        self.fill(signal=0.5)
        self.assertIs(root.eval(), cancel)
        self.assertNotEqual(root.eval_path.seq_id, first)
        self.assertIs(root.eval_path.leaf, cancel)
        self.show('second walk', 'leaf', type(root.eval_path.leaf).__name__)

    def test_05_a_middle_node_can_be_asked_before_the_root_ever_ran(self) -> None:
        """Nothing about a node's own evaluation depends on a walk having happened."""
        root, signal, outer, inner, long_, cancel, flat = self._tree()
        self.fill(signal=5.0)

        self.assertIs(inner.eval(), True)
        self.assertEqual(signal.out.value, 5.0)
        self.assertEqual(len(root.eval_path), 0)  # no walk has been recorded
        self.assertIsNone(root.eval_path.leaf)

        self.assertIs(root.eval(), long_)
        self.assertEqual([node.type for node in root.eval_path], ['ROOT', 'BINARY', 'BINARY', 'LONGACTION'])


if __name__ == '__main__':
    unittest.main()
