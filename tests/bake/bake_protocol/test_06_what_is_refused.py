"""What a bake refuses, and what a refusal leaves behind.

A bake is the last thing that can report a graph as malformed, so what it
refuses is the point of it: an operator whose operands are not all bound, an
operator its node's arity does not apply, a node with no evaluation at all. The
other half of the same fact is what a refused bake does NOT do - it locks
nothing, seals nothing and prepares nothing, so the graph a caller gets back is
exactly the graph it handed in.

That is what these cases measure: the refusal, the report that travels with it,
and the graph that survives it - which is still a build, and can be finished and
baked afterwards.
"""

import unittest

from decision_graph.decision_tree.bake.c_action import ClearAction, LongAction
from decision_graph.decision_tree.bake.c_bake import BakeReport
from decision_graph.decision_tree.bake.c_collections import LogicMapping
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_edge import FALSE_CONDITION
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, CallExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode
from decision_graph.decision_tree.exc import BakeFailureError

from bake_case import BakeCase


class TestWhatIsRefused(BakeCase):
    """The refusals, and the graph they leave alone."""

    def _call_tree(self):
        """A root whose branch is a call - a node with no evaluation at all."""
        with RootLogicNode(name='Calls') as root:
            with self.mapping:
                with CallExpression(ExpressionOperator.add, [self.const(1)], 'spread'):
                    LongAction()
        return root

    def test_00_a_call_is_refused(self) -> None:
        root = self._call_tree()

        with self.assertRaises(BakeFailureError) as caught:
            root.bake()

        report = caught.exception.report
        self.assertIsInstance(report, BakeReport)
        self.assertEqual(report.code_name, 'TYPE')
        self.assertEqual(report.errors, 1)
        self.assertEqual(report.node.__class__.__name__, 'CallExpression')
        self.show('call refused', report)

    def test_01_an_operator_the_arity_does_not_apply_is_refused(self) -> None:
        with RootLogicNode(name='Operators') as root:
            with self.mapping:
                signal = self.mapping['signal']
                odd = BinaryExpression(ExpressionOperator.neg, signal, self.const(1))  # a unary op, two operands
                with odd:
                    LongAction()

        with self.assertRaises(BakeFailureError) as caught:
            root.bake()

        report = caught.exception.report
        self.assertEqual(report.code_name, 'TYPE')
        self.assertIs(report.node, odd)  # the node that would have refused to evaluate

    def test_02_the_failure_names_the_node(self) -> None:
        root = self._call_tree()

        with self.assertRaises(BakeFailureError) as caught:
            root.bake()
        self.assertIn('CallExpression', str(caught.exception))
        self.assertIn('TYPE', str(caught.exception))

    def test_03_a_refused_bake_locks_nothing(self) -> None:
        root = self._call_tree()

        with self.assertRaises(BakeFailureError) as caught:
            root.bake()

        report = caught.exception.report
        self.assertEqual(report.locked, 0)
        self.assertEqual(report.sealed, 0)
        self.assertEqual(report.capacity, 0)

        # ... which is not something a caller has to take on trust: the store is
        # still open, and the root's record still has no room.
        self.assertFalse(self.mapping.frozen)
        self.assertEqual(root.eval_path.capacity, 0)

    def test_04_a_graph_that_was_refused_is_still_a_graph(self) -> None:
        with RootLogicNode(name='Half') as root:
            with self.mapping:
                signal = self.mapping['signal']
                branch = BinaryExpression(ExpressionOperator.gt, signal, self.const(0))
                with branch:
                    LongAction()

        # Detached, so the branch has one arm where its arity reads two.
        kept = list(branch.children.values())[0]
        kept.detach()

        with self.assertRaises(BakeFailureError):
            root.bake()

        # The graph is still a build: the arm goes back, and the bake that
        # refused it before is the bake that accepts it now.
        branch.append(ClearAction(), FALSE_CONDITION)
        report = root.bake()
        self.assertEqual(report.code_name, 'OK')
        self.assertGreater(report.locked, 0)
        self.assertTrue(self.mapping.frozen)

    def test_05_a_failure_is_not_a_walk(self) -> None:
        root = self._call_tree()

        with self.assertRaises(BakeFailureError) as caught:
            root.bake()

        # The pass walks the graph, but nothing of a WALK happened: the record is
        # as empty as it was, and no node was given a value. The graph it walked
        # is the whole of it: the root, the call, the arm it took and the one it
        # never filled, and the literal the call was built over.
        self.assertEqual(len(root.eval_path), 0)
        self.assertEqual(caught.exception.report.nodes, 5)
        self.assertEqual(caught.exception.report.depth, 2)


if __name__ == '__main__':
    unittest.main()
