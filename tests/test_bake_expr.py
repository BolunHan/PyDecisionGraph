"""Wrapper tests for ``decision_graph.decision_tree.bake.c_expr``.

Scope is the Python surface only. How an operand is bound, and how a repr is
composed from the operands, are settled by ``tests/bake/test_c_expr.c``; what is
checked here is the wrapper's half of the bargain:

  - each arity has a class, and the class states the arity the node carries;
  - the operator a node was built with is the one it reports;
  - the bare head builds a node whose operands are bound one at a time, and an
    index past the end is refused;
  - an expression built in C comes back as the class its arity names.

Oracle: the C node's own reported type and operand count, read back through the
wrapper's properties, and the op codes handed in - never a re-derivation of the
wrapper's own arithmetic.
"""

import gc
import sys
import unittest

from decision_graph.decision_tree.bake.c_expr import (
    BinaryExpression,
    CallExpression,
    ExpressionNode,
    ExpressionOperator,
    TernaryExpression,
    UnaryExpression,
)
from decision_graph.decision_tree.bake.c_node import NODE_REGISTRY, LogicNode, PlaceholderNode


class TestUnaryExpression(unittest.TestCase):
    """Contract: a unary expression is one operand and one operator.

    Expected behavior:
        - the C node reports the UNARY type and a single operand slot;
        - the operator it was built with is the one reported back.

    The arity belongs to the class: an expression's operand count is fixed when
    it is built, so a class that took any number would only be re-checking it.
    """

    def test_00_a_unary_expression_reports_its_type_and_arity(self) -> None:
        """One operand, and the C node says so."""
        node = UnaryExpression(ExpressionOperator.neg, PlaceholderNode())
        self.assertEqual(node.type, 'UNARY')
        self.assertEqual(node.n_args, 1)

    def test_01_the_operator_is_the_one_given(self) -> None:
        """The op code reaches the C node."""
        self.assertEqual(UnaryExpression(ExpressionOperator.not_, PlaceholderNode()).op, ExpressionOperator.not_)

    def test_02_the_operand_is_reflected_in_the_display_text(self) -> None:
        """The repr is composed from the operand, not left empty."""
        operand = PlaceholderNode()
        node = UnaryExpression(ExpressionOperator.neg, operand)
        self.assertIsInstance(node.repr, str)
        self.assertIn(operand.repr, node.repr)


class TestBinaryExpression(unittest.TestCase):
    """Contract: a binary expression is two operands, left one first.

    Expected behavior:
        - the C node reports the BINARY type and two operand slots;
        - the operator is the one given.

    The order matters and is the caller's: the left operand is the one passed
    first.
    """

    def test_00_a_binary_expression_reports_its_type_and_arity(self) -> None:
        """Two operands, and the C node says so."""
        node = BinaryExpression(ExpressionOperator.add, PlaceholderNode(), PlaceholderNode())
        self.assertEqual(node.type, 'BINARY')
        self.assertEqual(node.n_args, 2)

    def test_01_the_operator_is_the_one_given(self) -> None:
        """The op code reaches the C node."""
        node = BinaryExpression(ExpressionOperator.add, PlaceholderNode(), PlaceholderNode())
        self.assertEqual(node.op, ExpressionOperator.add)

    def test_02_both_operands_reach_the_display_text(self) -> None:
        """The repr names both operands."""
        left = PlaceholderNode()
        right = PlaceholderNode()
        node = BinaryExpression(ExpressionOperator.add, left, right)
        self.assertIn(left.repr, node.repr)
        self.assertIn(right.repr, node.repr)


class TestTernaryExpression(unittest.TestCase):
    """Contract: a ternary expression is condition, then and else.

    Expected behavior:
        - the C node reports the TERNARY type and three operand slots.
    """

    def test_00_a_ternary_expression_reports_its_type_and_arity(self) -> None:
        """Three operands, and the C node says so."""
        node = TernaryExpression(ExpressionOperator.none, PlaceholderNode(), PlaceholderNode(), PlaceholderNode())
        self.assertEqual(node.type, 'TERNARY')
        self.assertEqual(node.n_args, 3)


class TestCallExpression(unittest.TestCase):
    """Contract: a call is variadic, and is written as the function it calls.

    Expected behavior:
        - the C node reports the CALL type and one slot per argument;
        - the name given is what the display text is built around;
        - no arguments is refused, and so is an argument that is not a node.

    A call has no operator symbol of its own, which is why its name is given
    rather than inferred.
    """

    def test_00_a_call_reports_its_type_and_argument_count(self) -> None:
        """One slot per argument, and the C node says so."""
        node = CallExpression(ExpressionOperator.none, [PlaceholderNode(), PlaceholderNode()], name='func')
        self.assertEqual(node.type, 'CALL')
        self.assertEqual(node.n_args, 2)

    def test_01_the_name_is_what_the_display_text_is_built_around(self) -> None:
        """The callee's name is in the repr."""
        node = CallExpression(ExpressionOperator.none, [PlaceholderNode()], name='func')
        self.assertIn('func', node.repr)

    def test_02_an_empty_argument_list_is_refused(self) -> None:
        """A call with nothing to call is not a call."""
        with self.assertRaises(ValueError):
            CallExpression(ExpressionOperator.none, [], name='func')

    def test_03_a_non_node_argument_is_refused(self) -> None:
        """The argument type is the contract, and the error is the report."""
        with self.assertRaises(TypeError):
            CallExpression(ExpressionOperator.none, [object()], name='func')


class TestExpressionHead(unittest.TestCase):
    """Contract: the family head builds a node whose operands are bound later.

    Expected behavior:
        - with nothing given, the node carries the family type and the default
          operand count;
        - an operand binds into the slot it names;
        - an index past the end is refused rather than ignored.

    This is the path a node takes when its operands are not known at once.
    """

    def test_00_the_bare_head_reports_the_family_type(self) -> None:
        """With no type given, the node is an OP with the default operand count."""
        node = ExpressionNode()
        self.assertEqual(node.type, 'OP')
        self.assertEqual(node.n_args, 2)  # DCG_EXPR_DEFAULT_ARGS
        self.assertEqual(node.op, ExpressionOperator.none)

    def test_01_no_count_and_a_count_of_zero_mean_the_same(self) -> None:
        """Both sides read zero as "not given", and fall back to the same count.

        The wrapper's signature default and the C layer's clamp have to be the
        one number: a wrapper that sized its own bookkeeping by the argument it
        was handed, rather than by what the node reports, would be a slot short
        of the node it is holding.
        """
        self.assertEqual(ExpressionNode().n_args, ExpressionNode(0).n_args)

    def test_02_a_zero_count_still_holds_every_slot(self) -> None:
        """The operand array covers what the node was built with, not the zero."""
        node = ExpressionNode(0)
        for index in range(node.n_args):
            node.bind(index, PlaceholderNode())

    def test_03_the_operand_count_is_the_one_given(self) -> None:
        """A count given at construction is the count the node carries."""
        self.assertEqual(ExpressionNode(4).n_args, 4)

    def test_04_an_operand_binds_into_the_slot_it_names(self) -> None:
        """Binding is accepted for a slot that exists."""
        node = ExpressionNode(2)
        node.bind(0, PlaceholderNode())
        node.bind(1, PlaceholderNode())

    def test_05_an_index_past_the_end_is_refused(self) -> None:
        """A slot that does not exist is an error, not a silent no-op."""
        with self.assertRaises(RuntimeError):
            ExpressionNode(1).bind(1, PlaceholderNode())


class TestExprRegistration(unittest.TestCase):
    """Contract: a node rebuilt from C comes back as the class that built it.

    Expected behavior:
        - an address looked up after its wrapper is gone rebuilds as the
          expression's own class, not as the base.

    Every arity has its own variant (see ``test_bake_reconstruct.py``), so the
    class a rebuilt node gets is the one whose operand count it actually has.
    """

    def test_00_each_arity_rebuilds_as_its_own_class(self) -> None:
        """The registry misses, and the lookup rebuilds the right class."""
        nodes = (
            UnaryExpression(ExpressionOperator.neg, PlaceholderNode()),
            BinaryExpression(ExpressionOperator.add, PlaceholderNode(), PlaceholderNode()),
            TernaryExpression(ExpressionOperator.none, PlaceholderNode(), PlaceholderNode(), PlaceholderNode()),
        )
        for node in nodes:
            with self.subTest(expression=type(node).__name__):
                address = node.address
                del NODE_REGISTRY[address]
                self.assertIsInstance(NODE_REGISTRY[address], type(node))

    def test_01_a_call_rebuilds_as_the_generic_view(self) -> None:
        """A call is the one expression a reconstruction will not claim.

        What it calls is composed into the node's display text and stored
        nowhere, so a rebuilt CallExpression would not know its callee. The
        generic node view stands for the block instead - faithfully, repr and
        all - and claims nothing it cannot back.
        """
        node = CallExpression(ExpressionOperator.none, [PlaceholderNode()], name='func')
        address = node.address
        del NODE_REGISTRY[address]

        rebuilt = NODE_REGISTRY[address]
        self.assertIs(type(rebuilt), LogicNode)
        self.assertEqual(rebuilt.type, 'CALL')
        self.assertEqual(rebuilt.repr, node.repr)


class TestExpressionOperator(unittest.TestCase):
    """Contract: the op codes are an enum, not loose names on the module.

    Expected behavior:
        - the four family masks are members, and every variant under them;
        - each member carries its own code, so no two operators are the same
          number.

    The enum is the surface because a cimported C constant is not a module
    attribute: without it a caller would spell operators as bare numbers and
    have nothing to check them against.
    """

    def test_00_every_family_mask_is_a_member(self) -> None:
        """The four families a call site can ask about are members."""
        for member in (
            ExpressionOperator.arith,
            ExpressionOperator.compare,
            ExpressionOperator.logic,
            ExpressionOperator.access,
        ):
            with self.subTest(member=member.name):
                self.assertIsInstance(member, int)

    def test_01_the_members_carry_distinct_codes(self) -> None:
        """Two operators do not share a code."""
        codes = [member.value for member in ExpressionOperator]
        self.assertEqual(len(codes), len(set(codes)))

    def test_02_a_member_is_the_code_the_node_reports(self) -> None:
        """The enum and the C node agree on the operator."""
        node = BinaryExpression(ExpressionOperator.mul, ExpressionNode(), ExpressionNode())
        self.assertEqual(node.op, ExpressionOperator.mul)


class TestExpressionOperators(unittest.TestCase):
    """Contract: an operator over expressions yields an expression.

    Expected behavior:
        - arithmetic, ordered comparison and logical operators each build the
          node named by the operator they are spelled as;
        - a unary operator builds a one-operand node and a binary one builds a
          two-operand node;
        - an operand that is not a node is refused.

    This is what lets a graph be written as the arithmetic it is rather than as
    a nest of constructors. ``__eq__`` and ``__ne__`` are deliberately not among
    them: they would have to answer with a node, and a node is not a bool, so
    every ``==`` in the layer - a test's included - would quietly become a graph
    operation.
    """

    def test_00_arithmetic_builds_a_binary_expression(self) -> None:
        """``+`` builds the node that adds two operands."""
        node = ExpressionNode() + ExpressionNode()
        self.assertIsInstance(node, BinaryExpression)
        self.assertEqual(node.op, ExpressionOperator.add)
        self.assertEqual(node.n_args, 2)

    def test_01_each_arithmetic_operator_names_itself(self) -> None:
        """Every arithmetic operator builds its own node."""
        for node, member in (
            (ExpressionNode() - ExpressionNode(), ExpressionOperator.sub),
            (ExpressionNode() * ExpressionNode(), ExpressionOperator.mul),
            (ExpressionNode() / ExpressionNode(), ExpressionOperator.div),
            (ExpressionNode() // ExpressionNode(), ExpressionOperator.floordiv),
            (ExpressionNode() ** ExpressionNode(), ExpressionOperator.pow),
        ):
            with self.subTest(operator=member.name):
                self.assertEqual(node.op, member)

    def test_02_a_unary_operator_builds_a_one_operand_node(self) -> None:
        """Negation and inversion take a single operand."""
        for node, member in (
            (-ExpressionNode(), ExpressionOperator.neg),
            (~ExpressionNode(), ExpressionOperator.not_),
        ):
            with self.subTest(operator=member.name):
                self.assertIsInstance(node, UnaryExpression)
                self.assertEqual(node.op, member)
                self.assertEqual(node.n_args, 1)

    def test_03_ordered_comparison_builds_a_comparison_node(self) -> None:
        """The ordering operators build their own nodes."""
        for node, member in (
            (ExpressionNode() < ExpressionNode(), ExpressionOperator.lt),
            (ExpressionNode() <= ExpressionNode(), ExpressionOperator.le),
            (ExpressionNode() > ExpressionNode(), ExpressionOperator.gt),
            (ExpressionNode() >= ExpressionNode(), ExpressionOperator.ge),
        ):
            with self.subTest(operator=member.name):
                self.assertEqual(node.op, member)

    def test_04_logical_operators_build_their_own_nodes(self) -> None:
        """``&`` and ``|`` build the logical nodes."""
        self.assertEqual((ExpressionNode() & ExpressionNode()).op, ExpressionOperator.and_)
        self.assertEqual((ExpressionNode() | ExpressionNode()).op, ExpressionOperator.or_)

    def test_05_a_python_value_is_a_literal_operand(self) -> None:
        """A value composes by becoming the literal that carries it.

        A node is taken as itself; a bool, an int, a float or a str becomes a
        ConstantNode in that operand's place, on either side of the operator. What
        is left to refuse is a value with no node type at all, and the error names
        it.
        """
        self.assertEqual((ExpressionNode() + 1).operands[1].type, 'INT')
        self.assertEqual((1 + ExpressionNode()).operands[0].type, 'INT')
        with self.assertRaises(TypeError):
            ExpressionNode() + None

    def test_06_the_result_is_a_node_of_the_graph(self) -> None:
        """A composed expression registers like any other node."""
        node = ExpressionNode() + ExpressionNode()
        self.assertIn(node.address, NODE_REGISTRY)


class TestOperandLifetime(unittest.TestCase):
    """Contract: an expression keeps the operands whose storage it reads.

    Expected behavior:
        - making a node an operand raises its reference count, so its wrapper is
          not collected while the expression reads through it;
        - an operand dropped by name and by registry entry is still a live node
          while the expression holds it.

    An expression's operands are values it refers to, not children it owns: the
    slot reads the operand's out, so a released operand would leave the
    expression reading a block that is gone. The C node holds the BLOCK and the
    wrapper holds the WRAPPER - the second is what keeps the node registered and
    identifiable, which is why deleting the name is not enough to lose it.
    """

    def test_00_an_operand_is_held_by_the_expression(self) -> None:
        """The wrapper's reference count rises when it becomes an operand."""
        operand = ExpressionNode()
        before = sys.getrefcount(operand)
        operand + ExpressionNode()
        self.assertGreater(sys.getrefcount(operand), before)

    def test_01_a_dropped_operand_is_still_a_live_node(self) -> None:
        """An operand outlives the caller that let it go."""
        operand = ExpressionNode()
        expression = operand + ExpressionNode()
        address = operand.address

        del NODE_REGISTRY[address]
        del operand
        gc.collect()

        rebuilt = NODE_REGISTRY[address]
        self.assertIsInstance(rebuilt, ExpressionNode)
        self.assertEqual(rebuilt.type, 'OP')
        self.assertEqual(expression.n_args, 2)

    def test_02_a_call_holds_every_argument(self) -> None:
        """A variadic call holds each of its arguments."""
        arguments = [ExpressionNode(), ExpressionNode()]
        before = [sys.getrefcount(argument) for argument in arguments]
        CallExpression(ExpressionOperator.none, arguments, name='func')
        for argument, count in zip(arguments, before):
            with self.subTest(argument=argument.address):
                self.assertGreater(sys.getrefcount(argument), count)

    def test_03_a_dropped_argument_survives_its_call(self) -> None:
        """The same holds through the variadic constructor."""
        argument = ExpressionNode()
        call = CallExpression(ExpressionOperator.none, [argument], name='func')
        address = argument.address

        del NODE_REGISTRY[address]
        del argument
        gc.collect()

        self.assertEqual(NODE_REGISTRY[address].type, 'OP')
        self.assertEqual(call.n_args, 1)
