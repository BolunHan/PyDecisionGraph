"""The verdict without the commitment: ``bake(validate_only=True)``.

A caller about to bake may want the answer to "would this bake?" first - at the
end of a build, or before a graph leaves the process that built it. A
validate-only bake is that question: it walks the graph and reports exactly what
a bake would find, and it changes nothing at all - no node locked, no store
sealed, no record prepared.

What makes it worth its own cases is the pair of claims that have to hold at
once: the verdict it gives is the SAME verdict a full bake gives, and the graph
it gave it about is the same graph afterwards.
"""

import unittest

from decision_graph.decision_tree.bake.c_action import ClearAction, LongAction
from decision_graph.decision_tree.bake.c_edge import ELSE_CONDITION
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, CallExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode
from decision_graph.decision_tree.exc import BakeFailureError

from bake_case import BakeCase


class TestValidateOnly(BakeCase):
    """The bake that changes nothing."""

    def test_00_the_verdict_is_the_whole_of_what_is_asked_for(self) -> None:
        tree = self.tree()

        report = tree.root.bake(validate_only=True)
        self.assertEqual(report.code_name, 'OK')
        self.assertEqual(report.errors, 0)

        # The same graph, checked the same way a bake checks it.
        full = tree.root.bake()
        self.assertEqual(report.nodes, full.nodes)
        self.assertEqual(report.depth, full.depth)
        self.show('validate only', report)

    def test_01_nothing_is_locked_or_sealed_or_prepared(self) -> None:
        tree = self.tree()

        report = tree.root.bake(validate_only=True)
        self.assertEqual(report.locked, 0)
        self.assertEqual(report.sealed, 0)
        self.assertEqual(report.capacity, 0)

        self.assertFalse(self.mapping.frozen)
        self.assertEqual(tree.root.eval_path.capacity, 0)

    def test_02_the_graph_is_still_a_build(self) -> None:
        tree = self.tree()
        tree.root.bake(validate_only=True)

        # Everything a baked graph refuses, a checked one still allows.
        tree.outer.bind(1, self.const(7))
        tree.flat.detach()
        late = ClearAction()
        tree.outer.append(late, ELSE_CONDITION)
        late.detach()

        with self.mapping:
            self.mapping['late'] = 1.0  # and the store is open
        self.assertFalse(self.mapping.frozen)

    def test_03_a_full_bake_afterwards_is_a_first_bake(self) -> None:
        tree = self.tree()
        tree.root.bake(validate_only=True)

        report = tree.root.bake()
        self.assertEqual(report.code_name, 'OK')
        self.assertEqual(report.locked, report.nodes)  # nothing had been locked before it
        self.assertEqual(report.sealed, 1)
        self.assertTrue(self.mapping.frozen)

    def test_04_a_graph_that_would_not_bake_is_refused_the_same_way(self) -> None:
        with RootLogicNode(name='Calls') as root:
            with self.mapping:
                call = CallExpression(ExpressionOperator.add, [self.const(1)], 'spread')
                with call:
                    LongAction()

        with self.assertRaises(BakeFailureError) as checked:
            root.bake(validate_only=True)
        with self.assertRaises(BakeFailureError) as baked:
            root.bake()

        # One refusal, asked two ways: the node, the code and the count of
        # problems do not depend on which bake was asked for.
        self.assertIs(checked.exception.report.node, baked.exception.report.node)
        self.assertEqual(checked.exception.report.code, baked.exception.report.code)
        self.assertEqual(checked.exception.report.errors, baked.exception.report.errors)

    def test_05_a_refused_graph_keeps_its_store_open(self) -> None:
        with RootLogicNode(name='Calls') as root:
            with self.mapping:
                with CallExpression(ExpressionOperator.add, [self.const(1)], 'spread'):
                    LongAction()

        with self.assertRaises(BakeFailureError):
            root.bake(validate_only=True)
        self.assertFalse(self.mapping.frozen)

        with self.mapping:
            self.mapping['still_open'] = 1.0


if __name__ == '__main__':
    unittest.main()
