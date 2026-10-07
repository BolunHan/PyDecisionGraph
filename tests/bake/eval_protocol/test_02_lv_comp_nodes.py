"""Level-1 composites: one operator over one read (or two), and what it makes.

The composition is the dunder: ``mapping['B'] + ConstantNode(5)`` is not an
arithmetic on values, it BUILDS the operator node whose operands are those two
nodes. What the Python expression produces is a node of the graph, and what the
test asks it for is the value it comes to hold.

What is asserted for each case is both halves of the contract:

  - the composite reports the operator's answer for the values of the moment,
    taken through the read;
  - the composite's own slot holds that answer, while the read's slot still holds
    the reference it resolved to - the workspace is the expression's, and the
    operand's storage stays the store's.
"""

import unittest

from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_expr import (
    BinaryExpression,
    ExpressionOperator,
    UnaryExpression,
)

from eval_case import EvalCase


class TestOneOperand(EvalCase):
    """One read, one literal, one kernel - the smallest composite there is."""

    def test_00_a_read_plus_a_literal(self) -> None:
        """``mapping['B'] + 5``: the composite the directive names."""
        read = self.read('B')
        five = self.const(5)
        total = read + five

        self.assertEqual(total.type, 'BINARY')
        self.assertEqual(total.op, ExpressionOperator.add)
        self.assertEqual(total.n_args, 2)
        self.assertIs(total.operands[0], read)  # the read, held by the expression
        self.assertIs(total.operands[1], five)  # and the literal beside it

        self.fill(B=3)
        self.assertEqual(total.eval(), 8)
        self.assertEqual(total.out.value, 8)  # the composite's own slot
        self.assertEqual(read.eval(), 3)  # and the read still answers with the entry
        self.show('B=3  ->  B + 5', total.eval(), total.out.format())

        self.fill(B=10)
        self.assertEqual(total.eval(), 15)  # the same node, a new value
        self.show('B=10 ->  B + 5', total.eval())

    def test_01_the_composite_can_be_built_before_the_value_arrives(self) -> None:
        """The graph is written first; the store answers when it is asked."""
        read = self.read('late')
        total = read * self.const(2)

        with self.assertRaises(RuntimeError) as caught:
            total.eval()
        self.assertIn('UNBOUND', str(caught.exception))

        self.fill(late=4)
        self.assertEqual(total.eval(), 8)

    def test_02_the_other_operand_order(self) -> None:
        """A literal on the left, the read on the right: the same kernel, other slots."""
        read = self.read('B')
        gap = self.const(10) - read
        self.fill(B=4)
        self.assertEqual(gap.eval(), 6)
        self.assertEqual(gap.out.value, 6)


class TestEveryOperator(EvalCase):
    """Each operator the dunder can build, evaluated over one read and one literal."""

    def test_00_the_arithmetic(self) -> None:
        cases = (
            (ExpressionOperator.add, 7, 3, 10),
            (ExpressionOperator.sub, 7, 3, 4),
            (ExpressionOperator.mul, 7, 3, 21),
            (ExpressionOperator.div, 7, 2, 3.5),
            (ExpressionOperator.floordiv, 7, 2, 3),
            (ExpressionOperator.pow, 2, 10, 1024),
        )
        for operator, x, y, expected in cases:
            with self.subTest(operator=operator.name):
                read = self.read('x')
                node = BinaryExpression(operator, read, self.const(y))
                self.fill(x=x)
                self.assertEqual(node.eval(), expected)
                self.show(f'x={x} {operator.name} {y}', node.eval())

    def test_01_the_comparisons(self) -> None:
        cases = (
            (ExpressionOperator.eq, 2, 2, True),
            (ExpressionOperator.ne, 2, 3, True),
            (ExpressionOperator.gt, 3, 2, True),
            (ExpressionOperator.ge, 2, 2, True),
            (ExpressionOperator.lt, 1, 2, True),
            (ExpressionOperator.le, 3, 2, False),
        )
        for operator, x, y, expected in cases:
            with self.subTest(operator=operator.name):
                read = self.read('x')
                node = BinaryExpression(operator, read, self.const(y))
                self.fill(x=x)
                self.assertIs(node.eval(), expected)
                self.show(f'x={x} {operator.name} {y}', node.eval())

    def test_02_the_logical_operators_answer_with_an_operand(self) -> None:
        """``and``/``or`` are Python's: the value is one of the operands, not a bool."""
        cases = (
            (ExpressionOperator.and_, 1, 5, 5),
            (ExpressionOperator.and_, 0, 5, 0),
            (ExpressionOperator.or_, 0, 5, 5),
            (ExpressionOperator.or_, 2, 5, 2),
        )
        for operator, x, y, expected in cases:
            with self.subTest(operator=operator.name, x=x):
                read = self.read('x')
                node = BinaryExpression(operator, read, self.const(y))
                self.fill(x=x)
                self.assertEqual(node.eval(), expected)

    def test_03_the_dunders_build_the_nodes_they_name(self) -> None:
        read = self.read('x')
        self.assertEqual((read + self.const(1)).op, ExpressionOperator.add)
        self.assertEqual((read - self.const(1)).op, ExpressionOperator.sub)
        self.assertEqual((read * self.const(1)).op, ExpressionOperator.mul)
        self.assertEqual((read / self.const(1)).op, ExpressionOperator.div)
        self.assertEqual((read // self.const(1)).op, ExpressionOperator.floordiv)
        self.assertEqual((read ** self.const(1)).op, ExpressionOperator.pow)
        self.assertEqual((read > self.const(1)).op, ExpressionOperator.gt)
        self.assertEqual((-read).op, ExpressionOperator.neg)
        self.assertEqual((~read).op, ExpressionOperator.not_)


class TestOperandTypes(EvalCase):
    """What the value type of a composite is, over the tags the operands carry."""

    def test_00_int_and_double_promote_to_double(self) -> None:
        read = self.read('x')
        node = read + self.const(2.5)
        self.fill(x=1)
        self.assertEqual(node.eval(), 3.5)
        self.assertIsInstance(node.eval(), float)
        self.assertEqual(node.out.type_name, 'double')

    def test_01_int_and_int_stay_int(self) -> None:
        read = self.read('x')
        node = read + self.const(2)
        self.fill(x=1)
        self.assertEqual(node.eval(), 3)
        self.assertIsInstance(node.eval(), int)

    def test_02_a_bool_operand_is_a_number_to_arithmetic(self) -> None:
        read = self.read('x')
        node = read + self.const(1)
        self.fill(x=True)
        self.assertEqual(node.eval(), 2)

    def test_03_a_read_of_a_string_entry(self) -> None:
        """A string is a value like any other: it composes, it just cannot be added."""
        read = self.read('s')
        self.fill(s='text')
        self.assertEqual(read.eval(), 'text')
        self.assertEqual(read.out.type_name, 'string_ref')

        refused = read + self.const(1)
        with self.assertRaises(RuntimeError) as caught:
            refused.eval()
        self.assertIn('MATH', str(caught.exception))


if __name__ == '__main__':
    unittest.main()
