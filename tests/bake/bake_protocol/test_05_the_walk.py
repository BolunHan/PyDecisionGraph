"""The walk: a graph has TWO edges, and a bake follows both.

A node's children are the branches its value selects between. Its OPERANDS are
the nodes its rule runs - and an operand is not a child of anything: it is what
the node was built from, and the evaluation reaches it through the node. A read
is the operand of a comparison, so a walk that followed children alone would walk
straight past every read, every literal and every operator composed into one.

What these cases measure is the reach: that the read is reached though no node
holds it as a child, that reaching it is what seals its store, and that an
operand named twice is one node reached once.
"""

import unittest

from decision_graph.decision_tree.bake.c_action import LongAction
from decision_graph.decision_tree.bake.c_collections import LogicMapping
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode

from bake_case import BakeCase, store_name


class TestTheWalk(BakeCase):
    """The operands a bake reaches that the tree does not hold."""

    @staticmethod
    def _is_held_as_a_child(root, wanted) -> bool:
        """Whether any node of the tree holds `wanted` among its children."""
        stack = [root]
        while stack:
            for child in stack.pop().children.values():
                if child is wanted:
                    return True
                stack.append(child)
        return False

    def _branch_tree(self, build, name, mapping):
        """A root over one branch, built by `build` from the store it is given."""
        with mapping:
            branch = build(mapping)
        with RootLogicNode(name=name) as root:
            with branch:
                LongAction()
        return root, branch

    def test_00_a_read_is_reached_though_it_is_nobody_s_child(self) -> None:
        tree = self.tree()

        self.assertFalse(self._is_held_as_a_child(tree.root, tree.signal))
        report = tree.root.bake()

        # Nothing in the tree holds the read - and the store behind it is sealed,
        # which only a walk that ran the operands could have done.
        self.assertTrue(self.mapping.frozen)
        self.assertEqual(report.sealed, 1)

    def test_01_an_operand_of_an_operand_is_walked_too(self) -> None:
        with self.mapping:
            signal = self.mapping['signal']
            sum_ = BinaryExpression(ExpressionOperator.add, signal, self.const(1))
        branch = BinaryExpression(ExpressionOperator.gt, sum_, self.const(0))
        with RootLogicNode(name='Nested') as root:
            with branch:
                LongAction()

        report = root.bake()

        # The store is two operand hops from the tree - read by the sum, which
        # the comparison is built over - and the bake still reaches it.
        self.assertTrue(self.mapping.frozen)
        # root, branch, two arms, the sum, the read and two literals.
        self.assertEqual(report.nodes, 8)

    def test_02_an_operand_named_twice_is_one_node_walked_once(self) -> None:
        def with_literal(mapping):
            return BinaryExpression(ExpressionOperator.gt, mapping['signal'], self.const(0))

        def with_itself(mapping):
            signal = mapping['signal']
            return BinaryExpression(ExpressionOperator.gt, signal, signal)  # the same read twice

        first, _ = self._branch_tree(with_literal, 'Once', self.mapping)
        second, _ = self._branch_tree(with_itself, 'Twice', LogicMapping(name=store_name()))

        report_first = first.bake()
        report_second = second.bake()

        # One operand slot names a literal and the other names the read, so the
        # first graph has one node more - a count of EDGES would have called them
        # equal, and a walk that visited what it had already visited would have
        # made them equal the other way.
        self.assertEqual(report_first.nodes, report_second.nodes + 1)
        self.assertEqual(report_first.sealed, report_second.sealed)

    def test_03_the_operands_are_walked_in_a_branch_that_was_never_entered(self) -> None:
        tree = self.tree()

        # The dead arm's operands are still the graph's: the branch it hangs
        # under was reached, and both arms of it are children.
        report = tree.root.bake()
        self.assertEqual(report.nodes, 9)
        self.assertEqual(report.depth, 3)


if __name__ == '__main__':
    unittest.main()
