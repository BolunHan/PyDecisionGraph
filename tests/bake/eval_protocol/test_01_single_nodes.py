"""One node, on its own: what each type evaluates to, and what it refuses.

Every node type of the layer is asked for a value here, with nothing under it
and nothing over it. What the C layer says about this is exact and worth
restating, because the whole protocol follows from it:

  - a LITERAL's value is settled when it is built, so evaluating one is nothing
    and the slot it holds is the value;
  - an ACTION, a PLACEHOLDER and a ROOT stand for THEMSELVES - their slot holds
    their own address, written at birth - so evaluating one is nothing either,
    and what a caller reads out of it is a pointer, not a scalar;
  - a READ answers with what its store's entry holds, and only it has anything
    to do at run time;
  - an EXPRESSION runs its operands and applies one kernel;
  - a CALL has no callee to apply (its name is display text), so it refuses.

The refusals are asserted here too, with the message the layer builds: the node
that refused, the code it refused with, the stages it got through, and whether
it was part of a run.
"""

import unittest

from decision_graph.decision_tree.bake.c_action import (
    CancelAction,
    ClearAction,
    LongAction,
    NoAction,
    ShortAction,
)
from decision_graph.decision_tree.bake.c_const import VariableNode
from decision_graph.decision_tree.bake.c_expr import (
    CallExpression,
    ExpressionOperator,
    UnaryExpression,
)
from decision_graph.decision_tree.bake.c_node import PlaceholderNode

from eval_case import EvalCase


class TestLiteralNodes(EvalCase):
    """A literal is its value, and the value's type is the node's type."""

    def test_00_a_literal_evaluates_to_itself(self) -> None:
        cases = (
            (True, 'TRUE', 'bool'),
            (False, 'FALSE', 'bool'),
            (7, 'INT', 'int'),
            (-3, 'INT', 'int'),
            (2.5, 'DOUBLE', 'double'),
            ('text', 'STRING', 'string'),
        )
        for value, type_name, var_type in cases:
            with self.subTest(value=value):
                node = self.const(value)
                self.assertEqual(node.eval(), value)
                self.assertEqual(node.type, type_name)
                self.assertEqual(node.out.type_name, var_type)
                self.assertFalse(node.out.is_null)
                self.show(f'{type_name:<7} evaluates to', repr(node.eval()), f'({node.out.format()})')

    def test_01_a_literal_holds_its_value_without_being_evaluated(self) -> None:
        """The slot is written when the node is BUILT: an evaluation adds nothing."""
        node = self.const(4)
        before = node.out.value
        node.eval()
        self.assertEqual(node.out.value, before)
        self.assertEqual(node.out.value, 4)

    def test_02_a_standalone_read_reflects_nothing_until_it_is_bound(self) -> None:
        """A variable built with no slot has no answer, and says which nothing it is."""
        node = VariableNode()
        self.assertIsNone(node.key)
        self.assertIsNone(node.logic_group)
        with self.assertRaises(RuntimeError) as caught:
            node.eval()
        self.assertIn('VARIABLE', str(caught.exception))
        self.assertIn('UNBOUND', str(caught.exception))

    def test_03_a_read_bound_to_a_literal_follows_it(self) -> None:
        """A bound read is live: it reads the literal's slot, not a copy of it."""
        literal = self.const(5)
        node = VariableNode()
        node.c_bind_const(literal)
        self.assertEqual(node.eval(), 5)
        self.assertTrue(node.out.is_ref)


class TestSelfStandingNodes(EvalCase):
    """The nodes whose value is themselves: nothing runs, and the slot says so."""

    def test_00_an_action_leaf_stands_for_itself(self) -> None:
        """Its slot holds the NODE - and a node-shaped value unpacks as its wrapper."""
        for cls in (LongAction, ShortAction, CancelAction, ClearAction, NoAction):
            with self.subTest(action=cls.__name__):
                leaf = cls()
                self.assertIs(leaf.eval(), leaf)  # the node IS the value
                self.assertEqual(leaf.out.type_name, 'node')
                self.assertFalse(leaf.out.is_null)  # it holds its own address, not nothing
                self.assertEqual(leaf.out.format(), f'node {leaf.address:#x}')
                self.show(f'{cls.__name__:<12} out', leaf.out.format())

    def test_01_a_placeholder_stands_for_itself_too(self) -> None:
        """Which is what a branch built over one reads before the build fills it."""
        node = PlaceholderNode()
        self.assertEqual(node.type, 'PLACEHOLDER')
        self.assertTrue(node.autogen)
        self.assertIs(node.eval(), node)
        self.assertEqual(node.out.type_name, 'node')

    def test_02_a_breakpoint_passes_through(self) -> None:
        """It produces nothing and refuses nothing: inspection is not evaluation."""
        from decision_graph.decision_tree.bake.c_hierarchy import BreakpointNode

        sink = BreakpointNode()
        self.assertIsNone(sink.eval())


class TestExpressionNodes(EvalCase):
    """One operator node, its operands bound to literals."""

    def test_00_a_unary_node_applies_one_kernel(self) -> None:
        self.assertEqual(UnaryExpression(ExpressionOperator.neg, self.const(3)).eval(), -3)
        self.assertEqual(UnaryExpression(ExpressionOperator.not_, self.const(0)).eval(), True)
        self.assertEqual(UnaryExpression(ExpressionOperator.not_, self.const(3)).eval(), False)
        self.assertEqual(UnaryExpression(ExpressionOperator.neg, self.const(2.5)).eval(), -2.5)

    def test_01_an_operand_that_cannot_be_applied_is_refused(self) -> None:
        """A string has no arithmetic: the operator's own domain is what refuses."""
        node = UnaryExpression(ExpressionOperator.neg, self.const('text'))
        with self.assertRaises(RuntimeError) as caught:
            node.eval()
        self.assertIn('MATH', str(caught.exception))
        self.assertIn('no run', str(caught.exception))
        self.show('a negated string', caught.exception)

    def test_02_a_call_has_no_evaluation(self) -> None:
        """The callee is composed into the display text and stored nowhere."""
        node = CallExpression(ExpressionOperator.none, [self.const(1)], name='spread')
        self.assertEqual(node.type, 'CALL')
        with self.assertRaises(RuntimeError) as caught:
            node.eval()
        self.assertIn('TYPE', str(caught.exception))
        self.assertIn('CALL', str(caught.exception))
        self.show('a call node', caught.exception)

    def test_03_a_failure_reports_the_node_the_code_and_the_stages(self) -> None:
        """The message is the protocol's report: what refused, with what, how far it got."""
        node = UnaryExpression(ExpressionOperator.neg, self.const('text'))
        with self.assertRaises(RuntimeError) as caught:
            node.eval()
        message = str(caught.exception)
        self.assertIn('UNARY', message)  # the node that refused, by type
        self.assertIn(node.repr, message)  # and by its display text
        self.assertIn('MATH (-17)', message)  # the code, named and numbered
        self.assertIn('stages completed: pre', message)  # it stopped before its value existed
        self.assertIn('no run', message)  # one node is not a run
        self.assertIn('0x', message)  # and where it stands


if __name__ == '__main__':
    unittest.main()
