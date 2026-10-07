"""A baked graph decides: the same way before and after, and again and again.

Everything else in this suite is about what a bake does TO a graph. This file is
about the point of it: the graph a caller built still makes the decisions it made,
now with nothing in the way - the same values give the same leaf, and the store
the graph was baked over goes on driving it.

The deep case is built with an ``ExitStack`` rather than nested ``with`` blocks,
because the depth is a number in a test and a statement in a build: what the
stack does is exactly what nested blocks do - enter in order, leave in reverse.
"""

import unittest
from contextlib import ExitStack

from decision_graph.decision_tree.bake.c_action import LongAction
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode

from bake_case import BakeCase


class TestABakedGraphDecides(BakeCase):
    """The decision, before and after the bake."""

    def test_00_the_same_values_decide_the_same_way(self) -> None:
        tree = self.tree()

        decided = []
        for signal in (2.0, 0.5, -2.0):
            self.fill(signal=signal)
            decided.append(tree.root.eval())

        report = tree.root.bake()
        self.assertEqual(report.code_name, 'OK')

        self.fill(signal=2.0)
        self.assertIs(tree.root.eval(), decided[0])
        self.fill(signal=0.5)
        self.assertIs(tree.root.eval(), decided[1])
        self.fill(signal=-2.0)
        self.assertIs(tree.root.eval(), decided[2])

    def test_01_the_store_goes_on_driving_a_baked_graph(self) -> None:
        tree = self.tree()
        tree.root.bake()

        for signal, leaf in ((5.0, tree.long), (0.5, tree.cancel), (-5.0, tree.flat), (5.0, tree.long)):
            self.fill(signal=signal)
            self.assertIs(tree.root.eval(), leaf)
            self.show(f'signal={signal}', tree.root.eval_path.leaf)

    def test_02_every_walk_replaces_the_record(self) -> None:
        tree = self.tree()
        tree.root.bake()

        self.fill(signal=5.0)
        tree.root.eval()
        self.assertIs(tree.root.eval_path.leaf, tree.long)
        self.assertEqual(len(tree.root.eval_path), 4)

        self.fill(signal=-5.0)
        tree.root.eval()
        self.assertIs(tree.root.eval_path.leaf, tree.flat)
        self.assertEqual(len(tree.root.eval_path), 3)  # a shorter walk this time

    def test_03_a_dry_run_of_a_baked_graph_still_answers(self) -> None:
        tree = self.tree()
        tree.root.bake()

        self.fill(signal=5.0)
        self.assertIs(tree.outer.dry_run(), True)  # asked, not committed
        self.assertIs(tree.outer.out.is_null, True)  # and the node is as it was

    def test_04_a_deep_graph_is_baked_and_decides(self) -> None:
        levels = 6
        with RootLogicNode(name='Deep') as root:
            with self.mapping:
                signal = self.mapping['signal']
                with ExitStack() as stack:
                    for level in range(levels):
                        stack.enter_context(BinaryExpression(ExpressionOperator.gt, signal, self.const(levels - level)))
                    leaf = LongAction()

        report = root.bake()
        self.assertEqual(report.code_name, 'OK')
        # Per level: the comparison, the literal it compares against, and the
        # action its other arm was filled with - plus the root above them, the
        # read every one of them shares, and the leaf at the bottom.
        self.assertEqual(report.nodes, 3 * levels + 3)

        self.fill(signal=levels + 1)  # every comparison holds: the walk goes all the way down
        self.assertIs(root.eval(), leaf)
        self.assertEqual(root.eval_path.code_name, 'OK')
        self.assertEqual(len(root.eval_path), levels + 2)

        # None of them does: the walk turns at the first branch, into the arm the
        # build filled in for it, and stops there. What it landed on is a
        # no-action by its TYPE - the wrapper it comes back in is the one the
        # registry holds for the block, which the consolidation rewrote in place.
        self.fill(signal=0)
        landed = root.eval()
        self.assertEqual(str(landed.type), 'NOACTION')
        self.assertEqual(root.eval_path.code_name, 'OK')
        self.assertEqual(len(root.eval_path), 3)


if __name__ == '__main__':
    unittest.main()
