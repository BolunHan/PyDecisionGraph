"""The dry run: the answer comes back, and the node keeps what it had.

``dry_run`` is the evaluation asked as a QUESTION. The same three stages run as in
``eval`` - the hooks, the rule, the value - and then the node is put back the way
it was found: the value the evaluation produced is handed to the caller instead of
staying in the slot, and the slot is restored.

What that buys is a node that can be asked at any point of a build without
disturbing it: a branch a walk has already valued can be asked what it says NOW,
and the node under which a subtree is about to be walked can be asked first. What
it does NOT do is run the node's children - a dry run is one node, like an eval.
"""

import unittest

from decision_graph.decision_tree.bake.c_action import LongAction, ShortAction
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_expr import ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode

from eval_case import EvalCase


class TestTheDryRun(EvalCase):
    """What comes back, and what is left behind."""

    def test_00_a_literal_answers_with_itself(self) -> None:
        node = self.const(2.5)
        self.assertEqual(node.dry_run(), 2.5)
        self.assertEqual(node.out.value, 2.5)  # a literal's slot is its value either way

    def test_01_the_value_comes_back_and_the_slot_does_not_move(self) -> None:
        read = self.read('x')
        node = (read + self.const(5)) * self.const(2)
        self.fill(x=3)

        self.assertEqual(node.eval(), 16)
        self.assertEqual(node.out.value, 16)

        self.fill(x=10)
        self.assertEqual(node.dry_run(), 30)  # the question, answered
        self.assertEqual(node.out.value, 16)  # the slot: what the eval put there
        self.show('after a dry run', 'answered', 30, 'slot', node.out.value)

        self.assertEqual(node.eval(), 30)  # and the eval still works, on the new value
        self.assertEqual(node.out.value, 30)

    def test_02_the_dry_run_of_a_node_that_never_ran(self) -> None:
        read = self.read('x')
        node = read * self.const(4)
        self.assertTrue(node.out.is_null)  # nothing yet

        self.fill(x=2)
        self.assertEqual(node.dry_run(), 8)
        self.assertTrue(node.out.is_null)  # and nothing after: no value was left behind

    def test_03_the_answer_is_the_value_of_this_moment(self) -> None:
        read = self.read('x')
        node = read > self.const(0)
        for value, expected in ((1, True), (-1, False), (10, True)):
            with self.subTest(x=value):
                self.fill(x=value)
                self.assertIs(node.dry_run(), expected)

    def test_04_a_dry_run_of_a_composite_runs_its_operands(self) -> None:
        """One node, so its operand runs - and the middle of the tree is updated."""
        read = self.read('x')
        inner = read + self.const(1)
        top = inner * self.const(10)
        self.fill(x=1)

        self.assertEqual(top.dry_run(), 20)
        self.assertEqual(inner.out.value, 2)  # the operand ran, and kept its value
        self.assertEqual(read.out.value, 1)
        self.assertTrue(top.out.is_null)  # only the node ASKED is put back

    def test_05_a_dry_run_of_a_failing_node_reports_the_failure(self) -> None:
        read = self.read('x')
        node = read + self.const(1)
        with self.assertRaises(RuntimeError) as caught:
            node.dry_run()
        self.assertIn('UNBOUND', str(caught.exception))
        self.assertTrue(node.out.is_null)  # and nothing was left in the slot

        self.fill(x='text')
        with self.assertRaises(RuntimeError) as caught:
            node.dry_run()
        self.assertIn('MATH', str(caught.exception))

    def test_06_a_dry_run_of_a_read_leaves_it_unresolved(self) -> None:
        """The question leaves nothing behind - not even the resolution it caused.

        A read begins holding its entry's OFFSET and is resolved to the entry by
        an evaluation. A dry run of one runs that resolution too, and then puts
        the slot back: the node is the node it was, so the read is unresolved
        again, and a real evaluation is what resolves it for good.
        """
        read = self.read('s')
        self.fill(s='hello')
        self.assertEqual(read.out.type_name, 'inferred')

        self.assertEqual(read.dry_run(), 'hello')  # the answer
        self.assertEqual(read.out.type_name, 'inferred')  # and the read is as it was found

        self.assertEqual(read.eval(), 'hello')  # asking for real resolves it
        self.assertEqual(read.out.type_name, 'string_ref')


class TestADryRunInAGraph(EvalCase):
    """The dry run beside a walk, which is where a build would use it."""

    def _gate(self):
        with RootLogicNode(name='Entry') as root:
            with self.mapping:
                read = self.mapping['x']
                gate = read > ConstantNode(0)
                with gate:
                    LongAction()
                    ShortAction()
        return root, read, gate

    def test_00_a_branch_can_be_asked_before_the_walk(self) -> None:
        root, read, gate = self._gate()
        self.fill(x=5)

        self.assertIs(gate.dry_run(), True)  # what the branch says
        self.assertTrue(gate.out.is_null)  # and the branch was not left holding it

        self.assertEqual(root.eval().type, 'LONGACTION')  # the walk then decides the same way

    def test_01_a_node_a_walk_valued_can_be_asked_again(self) -> None:
        root, read, gate = self._gate()
        self.fill(x=5)
        root.eval()
        walked = gate.out.value
        seq_id = root.eval_path.seq_id

        self.fill(x=-5)
        self.assertIs(gate.dry_run(), False)  # the question, asked again
        self.assertIs(gate.out.value, walked)  # the walk's value is still what it holds
        self.assertIsNotNone(root.eval_path.leaf)  # and the record of that walk stands
        self.assertEqual(root.eval_path.seq_id, seq_id)

    def test_02_the_dry_run_is_not_a_visit(self) -> None:
        """It writes no run on the node: the walk after it is what counts the visit."""
        root, read, gate = self._gate()
        self.fill(x=5)

        gate.dry_run()
        self.assertTrue(gate.out.is_null)  # the node was left as it was found
        root.eval()
        self.assertIsNotNone(root.eval_path.leaf)

    def test_03_a_root_asked_this_way_answers_for_itself(self) -> None:
        """``dry_run`` is one node - a root's own value, never the graph's decision.

        The root's slot holds its truth from the moment it is built, and the walk
        from it is ``eval``: the two doors of a root are different questions.
        """
        root, read, gate = self._gate()
        self.fill(x=-5)

        self.assertIs(root.dry_run(), True)  # the root's own value, not the decision
        self.assertEqual(len(root.eval_path), 0)  # and no walk was recorded
        self.assertEqual(root.eval().type, 'SHORTACTION')  # the walk is what decides


if __name__ == '__main__':
    unittest.main()
