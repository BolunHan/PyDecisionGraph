"""Composites of composites: a tree of operators over reads, and its levels.

``-((const 5 + var C) * var D)`` is the directive's example, and it is the shape
that makes the protocol visible: three operator nodes stacked, each one's operand
being the node below it, and only the top is asked for a value. Evaluating the
top runs the whole tree - each operator produces its own operands first, which is
what reaches the reads at the bottom - and every level keeps the value it
produced, so the intermediate results are readable afterwards.

What a caller changing the store must see is that the SAME graph answers with new
values: nothing is cached between evaluations, and the intermediate slots are
overwritten in place each time the tree runs.
"""

import unittest

from decision_graph.decision_tree.bake.c_expr import (
    BinaryExpression,
    ExpressionOperator,
    TernaryExpression,
    UnaryExpression,
)

from eval_case import EvalCase


class TestTheDirectiveExample(EvalCase):
    """``-((const 5 + var C) * var D)``, level by level."""

    def _tree(self):
        """The three-level tree, with its three nodes named."""
        c = self.read('C')
        d = self.read('D')
        inner = c + self.const(5)
        product = inner * d
        top = -product
        return c, d, inner, product, top

    def test_00_the_three_levels_hold_their_own_values(self) -> None:
        c, d, inner, product, top = self._tree()
        self.fill(C=2, D=3)

        self.assertEqual(top.eval(), -21)
        self.assertEqual(inner.eval(), 7)
        self.assertEqual(product.eval(), 21)

        # Asking the top filled the levels below it: a node's operands are run
        # before it is, so their slots hold what this evaluation produced.
        self.assertEqual(inner.out.value, 7)
        self.assertEqual(product.out.value, 21)
        self.assertEqual(top.out.value, -21)
        self.assertEqual(c.out.value, 2)  # the read holds the entry, read through
        self.show('C=2 D=3', 'inner', inner.out.value, 'product', product.out.value, 'top', top.out.value)

    def test_01_the_same_tree_follows_the_store(self) -> None:
        c, d, inner, product, top = self._tree()
        for c_value, d_value, expected in ((2, 3, -21), (4, 3, -27), (10, 2, -30)):
            with self.subTest(C=c_value, D=d_value):
                self.fill(C=c_value, D=d_value)
                self.assertEqual(top.eval(), expected)
                self.assertEqual(inner.out.value, c_value + 5)
                self.assertEqual(product.out.value, (c_value + 5) * d_value)  # before the negation
                self.assertEqual(top.out.value, expected)
                self.show(f'C={c_value} D={d_value}', 'top', top.eval())

    def test_02_each_level_can_be_asked_on_its_own(self) -> None:
        """A middle node is a node: asking it runs ITS operands and nothing above."""
        c, d, inner, product, top = self._tree()
        self.fill(C=1, D=10)

        self.assertEqual(top.eval(), -60)
        self.assertEqual(product.eval(), 60)

        self.fill(C=10)
        self.assertEqual(product.eval(), 150)  # recomputed from the new C
        self.assertEqual(top.out.value, -60)  # the top still holds what it last produced
        self.show('after C=10', 'product', product.eval(), 'top slot', top.out.value)


class TestDeeperAndMixedShapes(EvalCase):
    """A four-level chain, a ternary in the middle, and a reused node."""

    def test_00_a_four_level_chain(self) -> None:
        a = self.read('A')
        b = self.read('B')
        level_1 = a + self.const(1)
        level_2 = level_1 * b
        level_3 = level_2 - self.const(2)
        level_4 = level_3 / self.const(2)

        self.fill(A=3, B=4)
        self.assertEqual(level_4.eval(), 7.0)
        self.assertEqual([level_1.out.value, level_2.out.value, level_3.out.value], [4, 16, 14])

        self.fill(A=0, B=0)
        self.assertEqual(level_4.eval(), -1.0)
        self.assertEqual(level_2.out.value, 0)

    def test_01_a_ternary_over_a_composite(self) -> None:
        """The condition is a composite, and only the arm it picks is run."""
        read = self.read('x')
        picked = TernaryExpression(
            ExpressionOperator.none,
            read > self.const(0),
            read * self.const(100),
            read * self.const(-1),
        )
        self.fill(x=3)
        self.assertEqual(picked.eval(), 300)

        self.fill(x=-3)
        self.assertEqual(picked.eval(), 3)
        self.show('x=-3', 'ternary', picked.eval())

    def test_02_a_node_used_twice_is_evaluated_as_an_operand(self) -> None:
        """The same wrapper in two slots: both operands run, both report the same."""
        read = self.read('x')
        node = BinaryExpression(ExpressionOperator.add, read, read)
        self.fill(x=4)
        self.assertEqual(node.eval(), 8)

    def test_03_a_composite_of_composites_is_still_a_node(self) -> None:
        """Its own slot, its own type, its own answer - at any depth."""
        read = self.read('x')
        deep = ((read + self.const(1)) * (read - self.const(1))) - self.const(1)
        self.fill(x=3)
        self.assertEqual(deep.eval(), 7)  # (4 * 2) - 1
        self.assertEqual(deep.type, 'BINARY')
        self.assertIsInstance(deep, BinaryExpression)
        self.assertEqual(deep.out.value, 7)

    def test_04_a_unary_level_over_a_composite(self) -> None:
        read = self.read('flag')
        self.fill(flag=0)
        self.assertIs(UnaryExpression(ExpressionOperator.not_, read).eval(), True)
        self.fill(flag=7)
        self.assertIs(UnaryExpression(ExpressionOperator.not_, read).eval(), False)
        self.assertIs(UnaryExpression(ExpressionOperator.not_, read - self.const(7)).eval(), True)


if __name__ == '__main__':
    unittest.main()
