"""The edges of the protocol: what is NOT run, what runs twice, and what refuses.

Everything here is a case the shape of the protocol predicts and a caller can
observe - which is what makes it a test of the protocol rather than of an
operator:

  - an operand is a node, so an expression's operand is RUN before it is read,
    and its own slot is what shows it;
  - a ternary reads its condition and then only the arm it picks, so the arm it
    does not pick is never run - a fact observable in that arm's slot;
  - a hook is an OVERRIDE of the rule, so a node that carries one is not evaluated
    by its type - and a hook that refuses stops the walk at that node;
  - a read is live but its TYPE is pinned, and a store that cannot answer stops
    the walk where the read is;
  - a breakpoint is an inspection point: it produces nothing, and a walk that
    lands on one has no decision to hand back.
"""

import unittest

from decision_graph.decision_tree.bake.c_action import ClearAction, LongAction, NoAction
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_expr import (
    BinaryExpression,
    ExpressionOperator,
    TernaryExpression,
)
from decision_graph.decision_tree.bake.c_hierarchy import BreakpointNode, RootLogicNode
from decision_graph.decision_tree.bake.c_edge import NO_CONDITION
from decision_graph.decision_tree.bake.c_var import VarType
from decision_graph.decision_tree.exc import EvalFailureError

from eval_case import EvalCase


class TestWhatIsNotRun(EvalCase):
    """The protocol reaches only what the decision needs."""

    def test_00_a_ternary_does_not_run_the_arm_it_does_not_take(self) -> None:
        """The strongest statement of laziness: the untaken arm holds NOTHING after."""
        taken = self.read('taken')
        untaken = self.read('untaken')

        picked = TernaryExpression(
            ExpressionOperator.none,
            self.const(True),
            taken + self.const(1),
            untaken + self.const(1),
        )
        self.fill(taken=1)

        self.assertEqual(picked.eval(), 2)
        self.assertFalse(taken.out.is_null)  # the arm taken ran
        self.assertTrue(untaken.out.is_null)  # the arm not taken never did
        self.assertEqual(untaken.out.type_name, 'inferred')
        self.show('after a true ternary', 'untaken arm', untaken.out.type_name)

    def test_01_the_other_arm_when_the_condition_flips(self) -> None:
        untaken = self.read('untaken')
        picked = TernaryExpression(
            ExpressionOperator.none,
            self.const(False),
            self.const(1),
            untaken + self.const(1),
        )
        self.fill(untaken=0)

        self.assertEqual(picked.eval(), 1)
        self.assertFalse(untaken.out.is_null)  # this time the right arm ran

    def test_02_an_operand_was_run_before_it_was_read(self) -> None:
        """The operand's own slot holds what this evaluation produced."""
        read = self.read('x')
        inner = read * self.const(3)
        outer = inner + self.const(1)
        self.fill(x=2)

        self.assertTrue(inner.out.is_null)
        self.assertEqual(outer.eval(), 7)
        self.assertEqual(inner.out.value, 6)
        self.assertEqual(inner.out.type_name, 'int')

    def test_03_an_operand_that_fails_stops_the_node_above_it(self) -> None:
        """A single node reports the node ASKED; the operand's own report is its own.

        The code is the operand's - the read is what could not answer - but the
        message names the node the caller asked, because that is the evaluation
        that failed. Asking the read itself is what reports the read.
        """
        read = self.read('x')
        inner = read + self.const(1)
        outer = inner * self.const(2)

        with self.assertRaises(RuntimeError) as caught:
            outer.eval()
        self.assertIn('UNBOUND', str(caught.exception))
        self.assertIn('BINARY', str(caught.exception))  # the node asked
        self.assertTrue(outer.out.is_null)  # nothing was produced above it
        self.assertTrue(inner.out.is_null)  # nor below it: the read refused first

        with self.assertRaises(RuntimeError) as caught:
            read.eval()
        self.assertIn('VARIABLE', str(caught.exception))  # the read, asked itself
        self.show('a failing operand', caught.exception)


class TestHooksOverrideTheRule(EvalCase):
    """A hook is installed INSTEAD of the type's rule, and can refuse a walk."""

    def test_00_a_hook_refusal_stops_the_walk_at_that_node(self) -> None:
        class Refusing(NoAction):
            def pre_eval_fn(self):
                return -17  # DCG_ERR_MATH, returned rather than raised

        with RootLogicNode(name='Entry') as root:
            with self.mapping:
                read = self.mapping['signal']
                with read > ConstantNode(0):
                    leaf = Refusing()

        self.fill(signal=5.0)
        with self.assertRaises(RuntimeError) as caught:
            root.eval()
        self.assertIn('MATH', str(caught.exception))

        path = root.eval_path
        self.assertEqual([node.type for node in path], ['ROOT', 'BINARY', 'NOACTION'])
        self.assertIs(path.failed, leaf)  # the hook refused it where it stood
        self.assertIsNone(path.leaf)
        self.show('a refusing hook', caught.exception)

    def test_01_a_hook_that_raises_in_a_walk_ends_it_as_a_code(self) -> None:
        """A walk reports the code it ended with; the exception stays with the node.

        The hook's own exception is kept by the node that raised it - asking THAT
        node re-raises it, which is what the single-node cases check - while a walk,
        which is a run of many nodes, reports HOOK and where it stopped.
        """
        class Broken(NoAction):
            def eval_fn(self):
                raise ValueError('the hook said no')

        with RootLogicNode(name='Entry') as root:
            with self.mapping:
                read = self.mapping['signal']
                with read > ConstantNode(0):
                    node = Broken()

        self.fill(signal=5.0)
        with self.assertRaises(EvalFailureError) as caught:
            root.eval()
        self.assertIn('HOOK', str(caught.exception))
        self.assertIs(root.eval_path.failed, node)  # the node that raised it
        self.assertEqual(root.eval_path.code_name, 'HOOK')

        # Asked itself, the node raises the same failure - and the hook's own
        # exception is its cause, not the thing that travels.
        with self.assertRaises(EvalFailureError) as own:
            node.eval()
        self.assertEqual(own.exception.code_name, 'HOOK')
        self.assertIsInstance(own.exception.__cause__, ValueError)
        self.assertEqual(str(own.exception.__cause__), 'the hook said no')
        self.show('a hook that raised inside a walk', caught.exception)

    def test_02_the_rule_is_what_runs_when_nothing_overrides_it(self) -> None:
        """A node that overrides no hook carries none, and its type's rule answers."""
        leaf = NoAction()
        self.assertEqual(leaf.eval_hooks, ())
        self.assertIs(leaf.eval(), leaf)  # the type's rule: an action stands for itself

        literal = self.const(3)
        self.assertEqual(literal.eval_hooks, ())
        self.assertEqual(literal.eval(), 3)


class TestRefusalsThatStopAWalk(EvalCase):
    """The codes a walk reports, and where they come from."""

    def test_00_a_store_that_cannot_answer_stops_the_walk(self) -> None:
        with RootLogicNode(name='Entry') as root:
            with self.mapping:
                read = self.mapping['never']
                with read > ConstantNode(0):
                    LongAction()

        with self.assertRaises(RuntimeError) as caught:
            root.eval()
        self.assertIn('UNBOUND', str(caught.exception))
        self.assertEqual(root.eval_path.code_name, 'UNBOUND')
        self.assertIsNone(root.eval_path.leaf)

    def test_01_an_operator_domain_failure_stops_the_walk(self) -> None:
        with RootLogicNode(name='Entry') as root:
            with self.mapping:
                read = self.mapping['x']
                with (read / ConstantNode(0)) > ConstantNode(1):
                    LongAction()

        self.fill(x=1)
        with self.assertRaises(RuntimeError) as caught:
            root.eval()
        self.assertIn('MATH', str(caught.exception))
        self.assertEqual(root.eval_path.code_name, 'MATH')

    def test_02_a_breakpoint_lands_with_nothing_to_hand_back(self) -> None:
        with RootLogicNode(name='Entry') as root:
            with self.mapping:
                sink = BreakpointNode(break_from=self.mapping)
                root.overwrite(sink, NO_CONDITION)

        self.assertIsNone(root.eval())  # an inspection is not a decision
        self.assertEqual([node.type for node in root.eval_path], ['ROOT', 'BREAKPOINT'])
        self.assertEqual(root.eval_path.code_name, 'OK')
        self.show('a breakpoint leaf', 'code', root.eval_path.code_name)


class TestTheReadsView(EvalCase):
    """What the slot of a read reports, before and after it resolves."""

    def test_00_before_a_value_arrives(self) -> None:
        read = self.read('x')
        self.assertTrue(read.out.is_null)
        self.assertEqual(read.out.type_name, 'inferred')
        self.assertTrue(read.out.is_ref)  # the entry by offset: a reference, with no type yet
        self.assertIs(read.out.ref_base, VarType.reserved)
        self.assertEqual(read.out.format(), '(reserved)')

    def test_01_after_a_value_arrives(self) -> None:
        read = self.read('x')
        self.fill(x=3)
        read.eval()

        self.assertFalse(read.out.is_null)
        self.assertTrue(read.out.is_ref)
        self.assertEqual(read.out.ref_level, 1)
        self.assertIs(read.out.ref_base, VarType.int)
        self.assertEqual(read.out.value, 3)
        self.assertEqual(read.out.as_int, 3)
        self.assertIn('3', read.out.format())  # the display text reads through the reference
        self.show('a resolved read', read.out.type_name, read.out.format())

    def test_02_the_answer_of_the_expression_is_not_a_reference(self) -> None:
        read = self.read('x')
        node = read + self.const(1)
        self.fill(x=1)
        node.eval()

        self.assertFalse(node.out.is_ref)  # the workspace was dereferenced, and the kernel wrote a value
        self.assertEqual(node.out.value, 2)
        self.assertEqual(node.out.type_name, 'int')
        self.assertEqual(node.out.format(), '2')

    def test_03_the_operand_slot_holds_the_value_not_the_slot(self) -> None:
        """What a rule reads off an operand is a literal: a reference is followed in."""
        read = self.read('x')
        node = read * self.const(2)
        self.fill(x=21)
        self.assertEqual(node.eval(), 42)
        self.assertTrue(read.out.is_ref)  # the read keeps the entry ...
        self.assertFalse(node.out.is_ref)  # ... and the expression holds the number


if __name__ == '__main__':
    unittest.main()
