"""Wrapper tests for ``decision_graph.decision_tree.bake.c_const``.

Scope is the Python surface only. What a constant node *is* in C is settled by
``tests/bake/test_c_const.c``; what is checked here is the wrapper's half of the
bargain:

  - one class covers the literals, and the C type follows the value;
  - the value read back is the value that went in;
  - a literal composes into an expression the same way an expression does,
    which is what makes a graph read as the arithmetic it is;
  - the layer's reconstruction brings a node built in C back as the class its
    type names, and this family's classes are what that answers with.

Oracle: the C node's own reported type and the value read back out of it, never
a re-derivation of the wrapper's own bookkeeping.
"""

import unittest

from decision_graph.decision_tree.bake.c_const import ConstantNode, VariableNode
from decision_graph.decision_tree.bake.c_expr import (
    BinaryExpression,
    ExpressionOperator,
    UnaryExpression,
)
from decision_graph.decision_tree.bake.c_node import NODE_REGISTRY

# The C type each Python literal is built as.
LITERALS = (
    (True, 'TRUE', True),
    (False, 'FALSE', False),
    (7, 'INT', 7),
    (2.5, 'DOUBLE', 2.5),
    ('text', 'STRING', 'text'),
)


class TestConstantTypes(unittest.TestCase):
    """Contract: the value chooses the C type.

    Expected behavior:
        - a bool is a TRUE or a FALSE node, an int an INT, a float a DOUBLE and
          a str a STRING;
        - a bool is not taken for an int, which it is a subclass of.

    One class covers the family because there is nothing for a caller to say
    that the value does not already say.
    """

    def test_00_the_value_chooses_the_type(self) -> None:
        """Each literal is built as its own C type."""
        for value, type_name, _ in LITERALS:
            with self.subTest(value=value):
                self.assertEqual(ConstantNode(value).type, type_name)

    def test_01_a_bool_is_not_an_int(self) -> None:
        """True is a boolean constant, not the integer one."""
        self.assertEqual(ConstantNode(True).type, 'TRUE')
        self.assertEqual(ConstantNode(1).type, 'INT')


class TestConstantValue(unittest.TestCase):
    """Contract: the value read back is the value that went in.

    Expected behavior:
        - the value survives the trip into C and back, type included.
    """

    def test_00_the_value_round_trips(self) -> None:
        """What went in comes back out."""
        for value, _, expected in LITERALS:
            with self.subTest(value=value):
                self.assertEqual(ConstantNode(value).value, expected)

    def test_01_a_negative_int_survives(self) -> None:
        """A signed literal is not truncated."""
        self.assertEqual(ConstantNode(-7).value, -7)

    def test_02_an_empty_string_is_a_constant(self) -> None:
        """The empty literal is a literal too."""
        self.assertEqual(ConstantNode('').value, '')

    def test_03_the_value_is_read_by_its_own_type(self) -> None:
        """The value is read for what it holds, not for the node's declared type.

        The constructor picks a node type that matches the value, so the two
        agree until something replaces the value alone - which the C setter does,
        because a literal's type is not a field the value writes. Reading by the
        node type would ask an INT reader for a double and truncate it, and for a
        string it would ask a reader the value cannot answer at all.

        Oracle: the C node's reported type, which stays what it was, against the
        values that came back.
        """
        node = ConstantNode(7)
        self.assertEqual(node.type, 'INT')

        with self.assertRaises(PermissionError):
            node.value = 2.5  # the debug setter writes, then refuses

        self.assertEqual(node.type, 'INT')  # the node's type did not follow the value
        self.assertEqual(node.value, 2.5)  # the value did

        with self.assertRaises(PermissionError):
            node.value = 'seven'  # a type with no numeric reader at all

        self.assertEqual(node.value, 'seven')


class TestConstantRepr(unittest.TestCase):
    """Contract: a literal names itself, or is named.

    Expected behavior:
        - the C layer renders the display text from the value;
        - a display text given at construction replaces it.
    """

    def test_00_the_value_is_the_default_display_text(self) -> None:
        """With nothing given, the literal renders itself."""
        self.assertEqual(ConstantNode(7).repr, '7')

    def test_01_a_given_repr_is_the_display_text(self) -> None:
        """An explicit display text wins."""
        self.assertEqual(ConstantNode(7, repr='seven').repr, 'seven')


class TestConstantRefusals(unittest.TestCase):
    """Contract: a value with no node type is refused.

    Expected behavior:
        - a container is not a constant: bake has no node type for it.
    """

    def test_00_a_list_is_refused(self) -> None:
        """A list has no constant type to be built as."""
        with self.assertRaises(TypeError):
            ConstantNode([1, 2, 3])

    def test_01_none_is_refused(self) -> None:
        """So has None."""
        with self.assertRaises(TypeError):
            ConstantNode(None)


class TestConstantOperators(unittest.TestCase):
    """Contract: a literal composes into an expression.

    Expected behavior:
        - an arithmetic or comparison operator over two constants yields the
          expression node for that operator;
        - the node class is reached by type, not by name, because the expression
          family is the same sub-layer and naming it would be an edge inside the
          layer.

    The operand is typed as the node base for the same reason: it is the one
    type both families can name.
    """

    def test_00_arithmetic_yields_an_expression(self) -> None:
        """A literal and a literal compose into a binary expression."""
        node = ConstantNode(1) + ConstantNode(2)
        self.assertIsInstance(node, BinaryExpression)
        self.assertEqual(node.op, ExpressionOperator.add)

    def test_01_a_literal_composes_with_an_expression(self) -> None:
        """And a literal with a bare expression."""
        node = ConstantNode(1) * UnaryExpression(ExpressionOperator.neg, ConstantNode(2))
        self.assertEqual(node.op, ExpressionOperator.mul)

    def test_02_a_unary_operator_yields_a_unary_expression(self) -> None:
        """Negation and inversion take the one operand they need."""
        self.assertIsInstance(-ConstantNode(1), UnaryExpression)
        self.assertEqual((-ConstantNode(1)).op, ExpressionOperator.neg)

    def test_03_comparison_yields_a_comparison_node(self) -> None:
        """Ordering a literal builds the comparison node."""
        self.assertEqual((ConstantNode(1) < ConstantNode(2)).op, ExpressionOperator.lt)

    def test_04_a_python_value_is_a_literal_operand(self) -> None:
        """A value composes by becoming the literal that carries it.

        A node is taken as itself; a bool, an int, a float or a str becomes a
        ConstantNode in that operand's place, on either side of the operator. What
        is left to refuse is a value with no node type at all, and the error names
        it.
        """
        self.assertEqual((ConstantNode(1) + 1).eval(), 2)
        self.assertEqual((1 + ConstantNode(1)).eval(), 2)
        with self.assertRaises(TypeError):
            ConstantNode(1) + None


class TestVariableNode(unittest.TestCase):
    """Contract: a variable is a read, not a holder.

    Expected behavior:
        - it reports its type, and the store entry it names;
        - with no entry named, it names none.

    A variable reflects a slot rather than carrying a value: what it reads lives
    somewhere else, and the node is the read of it. That is the difference from
    a constant, which carries the value itself.
    """

    def test_00_a_variable_reports_its_type(self) -> None:
        """A variable node is a VARIABLE."""
        self.assertEqual(VariableNode().type, 'VARIABLE')

    def test_01_an_entry_name_is_reported(self) -> None:
        """The store entry it names is kept."""
        self.assertEqual(VariableNode(key='close').key, 'close')

    def test_02_no_entry_name_reports_nothing(self) -> None:
        """With no entry named, there is nothing to name."""
        self.assertIsNone(VariableNode().key)


class TestVariableReflection(unittest.TestCase):
    """Contract: a variable reflects a slot, so what it reads can move.

    Expected behavior:
        - a variable with nothing bound reads nothing;
        - bound to a constant's slot, it reads what that slot holds;
        - when the slot moves, the read moves with it - the binding is a
          reference, not a copy taken when the binding was made;
        - writing a literal is refused, and the refusal is raised rather than
          reported as a value.

    This is the whole of the difference between a variable and a constant: a
    constant carries the value, a variable reads where the value lives. A read
    that answered with the value it saw at bind time would be a constant with
    extra steps.
    """

    def test_00_a_variable_with_nothing_bound_reads_nothing(self) -> None:
        """Nothing has been bound, so there is nothing to read."""
        self.assertIsNone(VariableNode().value)

    def test_01_a_bound_variable_reads_the_literal(self) -> None:
        """The read follows the slot it was bound to."""
        literal = ConstantNode(5)
        reader = VariableNode(key='n')
        reader.c_bind_const(literal)
        self.assertEqual(reader.value, 5)

    def test_02_the_read_follows_the_slot_when_it_moves(self) -> None:
        """The binding is a reference: what is read is live, not a snapshot."""
        literal = ConstantNode(5)
        reader = VariableNode(key='n')
        reader.c_bind_const(literal)

        with self.assertRaises(PermissionError):
            literal.value = 7

        self.assertEqual(reader.value, 7)

    def test_03_writing_a_literal_is_refused(self) -> None:
        """A literal is not redefinable, and the error names the class."""
        literal = ConstantNode(5)
        with self.assertRaises(PermissionError) as raised:
            literal.value = 7
        self.assertIn('ConstantNode', str(raised.exception))

    def test_04_a_reader_takes_a_literal_not_a_number(self) -> None:
        """The bind is typed: what a read reflects is a node, not a raw value."""
        with self.assertRaises(TypeError):
            VariableNode(key='n').c_bind_const(5)


class TestConstRegistration(unittest.TestCase):
    """Contract: a node rebuilt from C comes back as the class that built it.

    Expected behavior:
        - every literal type rebuilds as the constant class, and a variable as
          the variable class.

    The reconstruction is what turns a node back into the class it is (see
    ``test_bake_reconstruct.py``); the registry is what makes it the SAME one.
    """

    def test_00_a_literal_rebuilds_as_the_constant_class(self) -> None:
        """The literal families all come back as one class."""
        for value, type_name, _ in LITERALS:
            with self.subTest(type=type_name):
                node = ConstantNode(value)
                address = node.address
                del NODE_REGISTRY[address]
                self.assertIsInstance(NODE_REGISTRY[address], ConstantNode)

    def test_01_a_variable_rebuilds_as_the_variable_class(self) -> None:
        """A variable comes back as a variable, not as a constant."""
        node = VariableNode()
        address = node.address
        del NODE_REGISTRY[address]
        self.assertIsInstance(NODE_REGISTRY[address], VariableNode)
