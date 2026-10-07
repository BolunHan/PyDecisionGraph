"""The store: sealed in shape, and open for values.

A read holds a reference INTO its store's block, and that block moves only when
the store grows. So the half of a bake that an evaluation depends on is the
STORE's: sealing it is what keeps every read's reference good. What sealing does
not stop is a value - a store keeps its entries and a caller goes on writing
them, which is what "baked once, fed many times" means.

These cases are the store's side of the bake, and the reach that decides it: a
store is sealed because the graph READS it, and a store no read of the graph names
is none of the bake's business.
"""

import unittest

from decision_graph.decision_tree.bake.c_action import LongAction
from decision_graph.decision_tree.bake.c_collections import LogicMapping
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode

from bake_case import BakeCase


class TestTheStore(BakeCase):
    """What a bake does to the stores a graph reads."""

    def test_00_the_store_a_read_names_is_sealed(self) -> None:
        tree = self.tree()
        self.assertFalse(self.mapping.frozen)

        report = tree.root.bake()
        self.assertTrue(self.mapping.frozen)
        self.assertEqual(report.sealed, 1)

    def test_01_a_sealed_store_takes_no_new_entry(self) -> None:
        tree = self.tree()
        tree.root.bake()

        with self.assertRaises(RuntimeError) as caught:
            self.mapping['late'] = 1.0  # a write of a NEW key
        self.assertIn('-10', str(caught.exception))

        with self.assertRaises(KeyError):  # and a read of one, which is a miss
            self.read('late')

    def test_02_the_entries_a_sealed_store_has_go_on_taking_values(self) -> None:
        tree = self.tree()
        tree.root.bake()

        self.fill(signal=2.0)  # the entry the graph reads ...
        self.assertIs(tree.root.eval(), tree.long)

        with self.mapping:
            self.mapping['signal'] = -2.0  # ... written again, and again
        self.assertIs(tree.root.eval(), tree.flat)

    def test_03_an_entry_that_was_never_filled_is_not_a_problem(self) -> None:
        mapping = LogicMapping(name='reserved')
        with RootLogicNode(name='Entry') as root:
            with mapping:
                signal = mapping['signal']  # reserved, and nothing has landed in it
                with BinaryExpression(ExpressionOperator.gt, signal, ConstantNode(0)):
                    LongAction()

        report = root.bake()
        self.assertEqual(report.code_name, 'OK')
        self.assertTrue(mapping.frozen)

        # The entry the read names was reserved before the bake, and the value
        # arrives after it - which is the shape the layer builds for.
        with mapping:
            mapping['signal'] = 3.0
        self.assertIs(root.eval(), root.eval())
        self.assertEqual(root.eval_path.code_name, 'OK')

    def test_04_two_stores_in_one_graph_are_both_sealed(self) -> None:
        other = LogicMapping(name='store_b')
        with RootLogicNode(name='Entry') as root:
            with self.mapping:
                first = self.mapping['signal']
            with other:
                second = other['level']
            with BinaryExpression(ExpressionOperator.gt, first, second):
                LongAction()

        report = root.bake()
        self.assertTrue(self.mapping.frozen)
        self.assertTrue(other.frozen)
        self.assertEqual(report.sealed, 2)

    def test_05_a_store_the_graph_does_not_read_is_left_alone(self) -> None:
        bystander = LogicMapping(name='bystander')
        tree = self.tree()
        tree.root.bake()

        # What a bake seals is what the graph reads: a store nobody reads is not
        # part of the graph and not the bake's to close.
        self.assertFalse(bystander.frozen)
        with bystander:
            bystander['still_open'] = 1.0
        self.assertFalse(bystander.frozen)

    def test_06_the_seal_is_the_stores_own_not_the_bakes_alone(self) -> None:
        mapping = LogicMapping(name='manual')
        with RootLogicNode(name='Entry') as root:
            with mapping:
                signal = mapping['signal']
                with BinaryExpression(ExpressionOperator.gt, signal, ConstantNode(0)):
                    LongAction()

        # A caller may close a store's shape itself, at any time - what a bake
        # adds is doing it at the right moment, without being asked.
        mapping.frozen = True
        report = root.bake()
        self.assertEqual(report.sealed, 0)  # it was frozen already: nothing to seal
        self.assertEqual(report.code_name, 'OK')


if __name__ == '__main__':
    unittest.main()
