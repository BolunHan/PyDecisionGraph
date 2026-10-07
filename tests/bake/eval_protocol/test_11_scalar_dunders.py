"""Composing with Python values, on either side of the operator.

A composition is written with the dunder, and the operand may be a node OR a plain
value: ``5 + read`` and ``read + 5`` build the same node as the forms written out
with a ``ConstantNode``. What makes that work is one conversion - a value with no
node of its own becomes the literal that carries it - and, for the left-hand
side, the reflected dunder Python falls back to when ``int.__add__`` declines.

The three classes that compose carry the operators: an expression, a literal and
a read. What is checked here is the value the composite comes to hold, both ways
round, plus the operand ORDER for the operators where it matters and the refusal
of a value that has no literal type at all.
"""
import unittest

from decision_graph.decision_tree.bake.c_action import LongAction, ShortAction
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_expr import ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode

from eval_case import EvalCase

# Every operator, with the value it must produce for x=7 over a scalar 3 - the
# same answer the node-node form gives (test_02 checks that one).
CASES = (
    ('add', 7, 3, lambda x, y: x + y),
    ('sub', 7, 3, lambda x, y: x - y),
    ('mul', 7, 3, lambda x, y: x * y),
    ('truediv', 7, 2, lambda x, y: x / y),
    ('floordiv', 7, 2, lambda x, y: x // y),
    ('pow', 2, 10, lambda x, y: x ** y),
    ('eq', 7, 7, lambda x, y: x == y),
    ('ne', 7, 3, lambda x, y: x != y),
    ('gt', 7, 3, lambda x, y: x > y),
    ('ge', 7, 7, lambda x, y: x >= y),
    ('lt', 3, 7, lambda x, y: x < y),
    ('le', 7, 3, lambda x, y: x <= y),
    ('and', 7, 3, lambda x, y: x and y),
    ('or', 0, 3, lambda x, y: x or y),
)


# The operators that HAVE a reflected form: Python asks the right operand for
# these when the left one declines, and it reflects the COMPARISONS by itself (a
# `3 < read` calls `read.__gt__(3)`), which is why they are not in this list.
REFLECTED = tuple(case for case in CASES if case[0] not in ('eq', 'ne', 'lt', 'le', 'gt', 'ge'))


class TestScalarOperands(EvalCase):
    """The same operator, with the value on either side."""

    def test_00_a_value_on_the_right(self) -> None:
        for name, x, y, expected in CASES:
            with self.subTest(operator=name):
                read = self.read('x')
                node = getattr(read, f'__{name}__')(y)
                self.fill(x=x)
                self.assertEqual(node.eval(), expected(x, y))
                self.show(f'x={x} {name} {y}', node.eval())

    def test_01_a_value_on_the_left(self) -> None:
        for name, x, y, expected in REFLECTED:
            with self.subTest(operator=name):
                read = self.read('x')
                node = getattr(read, f'__r{name}__')(y)
                self.fill(x=x)
                self.assertEqual(node.eval(), expected(y, x))
                self.show(f'{y} {name} x={x}', node.eval())

    def test_02_a_comparison_with_the_value_on_the_left(self) -> None:
        """Written as Python writes it: the left operand declines, the read answers."""
        read = self.read('x')
        self.fill(x=7)
        self.assertIs((3 < read).eval(), True)   # read.__gt__(3)
        self.assertIs((7 <= read).eval(), True)  # read.__ge__(7)
        self.assertIs((9 > read).eval(), True)   # read.__lt__(9)
        self.assertIs((7 == read).eval(), True)  # read.__eq__(7)
        self.assertIs((3 != read).eval(), True)  # read.__ne__(3)

        # Python reflects a comparison by asking for the MIRROR operator, so the
        # node built from `3 <= read` is the one `read >= 3` builds: same operands,
        # same order - the literal stays on the right, where the read put it.
        mirrored = 3 <= read
        self.assertEqual(mirrored.op, ExpressionOperator.ge)
        self.assertIs(mirrored.operands[0], read)
        self.assertEqual(mirrored.operands[1].type, 'INT')
        self.assertIs(mirrored.operands[1].value, 3)

    def test_03_the_operand_order_is_kept_where_it_matters(self) -> None:
        """Subtraction and division are not symmetric: the value stays where it was written."""
        read = self.read('x')
        self.fill(x=10)
        self.assertEqual((read - 3).eval(), 7)
        self.assertEqual((3 - read).eval(), -7)
        self.assertEqual((read / 4).eval(), 2.5)
        self.assertEqual((4 / read).eval(), 0.4)
        self.assertEqual((2 ** read).eval(), 1024)
        self.assertEqual((read ** 2).eval(), 100)

    def test_04_the_literal_stands_in_the_operand_slot_it_was_written_in(self) -> None:
        read = self.read('x')
        self.fill(x=1)

        on_the_right = read + 5
        self.assertIs(on_the_right.operands[0], read)
        self.assertEqual(on_the_right.operands[1].type, 'INT')  # the literal that carries 5

        on_the_left = 5 + read
        self.assertEqual(on_the_left.operands[0].type, 'INT')
        self.assertIs(on_the_left.operands[1], read)
        self.assertEqual(on_the_left.op, ExpressionOperator.add)


class TestTheValuesThatCanStandIn(EvalCase):
    """Which Python values become literals, and which are refused."""

    def test_00_every_literal_type(self) -> None:
        read = self.read('x')
        self.fill(x=2)
        self.assertEqual((read + 3).eval(), 5)  # int
        self.assertEqual((read + 0.5).eval(), 2.5)  # float
        self.assertEqual((read + True).eval(), 3)  # bool, a number to arithmetic
        self.assertEqual((read * 3.0).eval(), 6.0)

    def test_01_a_string_composes_like_any_other_value(self) -> None:
        """It builds the node; the operator is what refuses it, at evaluation."""
        read = self.read('x')
        self.fill(x=1)
        texted = read + 'text'
        self.assertEqual(texted.operands[1].type, 'STRING')
        with self.assertRaises(RuntimeError) as caught:
            texted.eval()
        self.assertIn('MATH', str(caught.exception))

        self.assertEqual(('a' == read).eval(), False)  # comparison is not arithmetic

    def test_02_a_value_with_no_literal_type_is_refused(self) -> None:
        read = self.read('x')
        for value in (None, [1, 2], {'k': 1}):
            with self.subTest(value=value):
                with self.assertRaises(TypeError) as caught:
                    read + value
                self.assertIn('no node type', str(caught.exception))
                with self.assertRaises(TypeError):
                    value + read

    def test_03_a_node_is_still_taken_as_itself(self) -> None:
        """The conversion is a pass-through for a node, whichever class it is."""
        read = self.read('x')
        literal = ConstantNode(5)
        expression = literal + 1

        node = read + literal
        self.assertIs(node.operands[1], literal)
        self.assertIs((read + expression).operands[1], expression)
        self.assertIs((literal + read).operands[1], read)
        self.assertIs((expression + read).operands[0], expression)


class TestScalarsInAGraph(EvalCase):
    """Where a scalar operand is what a caller actually writes."""

    def test_00_a_branch_condition_against_a_scalar(self) -> None:
        with RootLogicNode(name='Entry') as root:
            with self.mapping:
                read = self.mapping['signal']
                with read > 0:
                    LongAction()
                    ShortAction()

        self.fill(signal=1.5)
        self.assertEqual(root.eval().type, 'LONGACTION')
        self.fill(signal=-1.5)
        self.assertEqual(root.eval().type, 'SHORTACTION')

    def test_01_a_threshold_written_with_the_value_on_the_left(self) -> None:
        with RootLogicNode(name='Entry') as root:
            with self.mapping:
                read = self.mapping['signal']
                with 0 < read:  # reflected: the same condition, written the other way
                    LongAction()
                    ShortAction()

        self.fill(signal=1.5)
        self.assertEqual(root.eval().type, 'LONGACTION')
        self.fill(signal=-1.5)
        self.assertEqual(root.eval().type, 'SHORTACTION')

    def test_02_a_whole_expression_over_scalars_and_reads(self) -> None:
        read = self.read('close')
        node = 100 - (read * 2 + 1)
        for close, expected in ((10.0, 79.0), (0.0, 99.0), (2.5, 94.0)):
            with self.subTest(close=close):
                self.fill(close=close)
                self.assertEqual(node.eval(), expected)
                self.show(f'close={close}', node.eval())


if __name__ == '__main__':
    unittest.main()
