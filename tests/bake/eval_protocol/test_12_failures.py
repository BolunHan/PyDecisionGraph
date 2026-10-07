"""A failed evaluation, and what the failure says about it.

Every door an evaluation is asked through - one node's ``eval``, a dry run, a walk
from a root - reports a failure the same way: ``EvalFailureError``, carrying the
node that refused, the code it ended with, the stage that failed, what was running
when it did, and the run it happened in.

The three questions it answers are the three a code alone cannot: WHICH node, at
which of the three stages (a node that refused in ``pre`` never produced a value,
and one that refused in ``eval`` never reached its post hook), and by WHAT - the
built-in evaluation, the rule the node's type carries, or a hook somebody put
there. A hook's own exception travels as the CAUSE, and a hook that raises this
error with a code of its own hands the protocol that code.
"""

import unittest

from decision_graph.decision_tree.bake.c_action import NoAction
from decision_graph.decision_tree.bake.c_const import ConstantNode, VariableNode
from decision_graph.decision_tree.bake.c_expr import ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode
from decision_graph.decision_tree.exc import EvalFailureError, NodeError

from eval_case import EvalCase


class TestWhatAFailureReports(EvalCase):
    """The fields, for the refusals the protocol has."""

    def test_00_a_read_with_nothing_in_it(self) -> None:
        read = self.read('x')
        node = read + self.const(1)

        with self.assertRaises(EvalFailureError) as caught:
            node.eval()
        failure = caught.exception

        self.assertEqual(failure.code_name, 'UNBOUND')
        self.assertEqual(failure.code, -18)
        self.assertIs(failure.node, node)  # the node the caller asked
        self.assertEqual(failure.node_type, 'BINARY')
        self.assertEqual(failure.node_repr, node.repr)
        self.assertEqual(failure.address, node.address)
        self.assertEqual(failure.source, 'type_rule')  # the rule the type carries
        self.assertEqual(failure.failed_at, 'eval')  # it stopped before its value existed
        self.assertEqual(failure.stages, ('pre',))  # and got that far
        self.assertEqual(failure.run_id, 0)  # one node is not a run
        self.show('a read with nothing in it', failure)

    def test_01_an_operand_outside_the_operator_domain(self) -> None:
        node = self.const('text') + self.const(1)
        with self.assertRaises(EvalFailureError) as caught:
            node.eval()
        self.assertEqual(caught.exception.code_name, 'MATH')
        self.assertEqual(caught.exception.failed_at, 'eval')
        self.assertEqual(caught.exception.source, 'type_rule')

    def test_02_a_call_refuses_by_its_own_rule(self) -> None:
        """A call carries the rule that says it has no callee to apply."""
        from decision_graph.decision_tree.bake.c_expr import CallExpression

        node = CallExpression(ExpressionOperator.none, [self.const(1)], name='spread')
        with self.assertRaises(EvalFailureError) as caught:
            node.eval()
        self.assertEqual(caught.exception.code_name, 'TYPE')
        self.assertEqual(caught.exception.source, 'type_rule')
        self.assertEqual(caught.exception.failed_at, 'eval')

    def test_03_a_read_of_no_store_is_the_dispatch_too(self) -> None:
        """A variable built by hand carries no rule: the built-in evaluation answers."""
        read = VariableNode()
        with self.assertRaises(EvalFailureError) as caught:
            read.eval()
        self.assertEqual(caught.exception.code_name, 'UNBOUND')
        self.assertEqual(caught.exception.source, 'builtin')
        self.assertEqual(caught.exception.node_type, 'VARIABLE')

    def test_04_a_failure_is_a_runtime_error_and_a_node_error(self) -> None:
        """What it inherits is what a caller can catch it as."""
        read = self.read('x')
        with self.assertRaises(RuntimeError):
            read.eval()
        with self.assertRaises(NodeError):
            read.eval()
        with self.assertRaises(EvalFailureError):
            read.eval()

    def test_05_the_message_names_the_node_the_code_the_stages_and_the_producer(self) -> None:
        read = self.read('x')
        node = read + self.const(1)
        with self.assertRaises(EvalFailureError) as caught:
            node.eval()
        message = str(caught.exception)
        self.assertIn('BINARY', message)
        self.assertIn(node.repr, message)
        self.assertIn('UNBOUND (-18)', message)
        self.assertIn('stages completed: pre', message)
        self.assertIn('no run', message)
        self.assertIn('stopped at eval in type_rule', message)
        self.show('the message', message)


class TestTheStagesAFailureStopsAt(EvalCase):
    """Which stage refused, told apart by what completed."""

    def test_00_a_pre_hook_refusal(self) -> None:
        class Refusing(NoAction):
            def pre_eval_fn(self):
                return -10  # DCG_ERR_BUSY

        with self.assertRaises(EvalFailureError) as caught:
            Refusing().eval()
        self.assertEqual(caught.exception.code_name, 'BUSY')
        self.assertEqual(caught.exception.source, 'pre_hook')
        self.assertEqual(caught.exception.failed_at, 'pre')
        self.assertEqual(caught.exception.stages, ())

    def test_01_an_eval_hook_refusal(self) -> None:
        class Refusing(NoAction):
            def eval_fn(self):
                return -17  # DCG_ERR_MATH

        with self.assertRaises(EvalFailureError) as caught:
            Refusing().eval()
        self.assertEqual(caught.exception.code_name, 'MATH')
        self.assertEqual(caught.exception.source, 'hook')
        self.assertEqual(caught.exception.failed_at, 'eval')
        self.assertEqual(caught.exception.stages, ('pre',))

    def test_02_a_post_hook_refusal(self) -> None:
        class Refusing(NoAction):
            def post_eval_fn(self):
                return -10

        with self.assertRaises(EvalFailureError) as caught:
            Refusing().eval()
        self.assertEqual(caught.exception.source, 'post_hook')
        self.assertEqual(caught.exception.failed_at, 'post')
        self.assertEqual(caught.exception.stages, ('pre', 'eval'))

    def test_03_a_dry_run_reports_the_same_failure(self) -> None:
        read = self.read('x')
        with self.assertRaises(EvalFailureError) as caught:
            read.dry_run()
        self.assertEqual(caught.exception.code_name, 'UNBOUND')
        self.assertEqual(caught.exception.failed_at, 'eval')


class TestAHooksOwnException(EvalCase):
    """What a hook says when it fails, and what the layer does with it."""

    def test_00_the_cause_is_kept(self) -> None:
        class Broken(NoAction):
            def eval_fn(self):
                raise KeyError('the hook said no')

        with self.assertRaises(EvalFailureError) as caught:
            Broken().eval()
        failure = caught.exception
        self.assertEqual(failure.code_name, 'HOOK')
        self.assertEqual(failure.source, 'hook')
        self.assertIsInstance(failure.__cause__, KeyError)
        self.assertEqual(str(failure.__cause__), "'the hook said no'")
        self.assertIn('HOOK', str(failure))

    def test_01_a_hook_that_carries_a_code_hands_it_to_the_node(self) -> None:
        """A Python hook says what went wrong the way a Cython hook does: with a code."""
        class Coded(NoAction):
            def eval_fn(self):
                raise EvalFailureError('the hook\'s own refusal', code=-17, code_name='MATH')

        with self.assertRaises(EvalFailureError) as caught:
            Coded().eval()
        self.assertEqual(caught.exception.code_name, 'MATH')
        self.assertEqual(caught.exception.code, -17)
        self.assertEqual(caught.exception.source, 'hook')
        self.assertEqual(caught.exception.failed_at, 'eval')
        # What the hook raised is the cause of what the layer reports: the hook's
        # message survives, and the node still ends with a code the protocol named.
        self.assertIsInstance(caught.exception.__cause__, EvalFailureError)
        self.assertEqual(str(caught.exception.__cause__.args[0]), "the hook's own refusal")

    def test_02_a_hook_that_carries_no_code_ends_as_the_hook_error(self) -> None:
        class Coded(NoAction):
            def eval_fn(self):
                raise EvalFailureError('no code to carry')

        with self.assertRaises(EvalFailureError) as caught:
            Coded().eval()
        self.assertEqual(caught.exception.code_name, 'HOOK')

    def test_03_a_hook_that_returns_a_code_is_still_a_return(self) -> None:
        """The other way a hook says no, unchanged: the returned code is the node's."""
        class Refusing(NoAction):
            def pre_eval_fn(self):
                return -18

        with self.assertRaises(EvalFailureError) as caught:
            Refusing().eval()
        self.assertEqual(caught.exception.code_name, 'UNBOUND')
        self.assertEqual(caught.exception.source, 'pre_hook')
        self.assertIsNone(caught.exception.__cause__)


class TestAFailureInAWalk(EvalCase):
    """What a walk reports, and what it does not."""

    def _graph(self):
        with RootLogicNode(name='Entry') as root:
            with self.mapping:
                read = self.mapping['signal']
                gate = read > ConstantNode(0)
                with gate:
                    NoAction()
        return root, read, gate

    def test_00_a_walk_reports_the_node_that_failed_and_the_run(self) -> None:
        root, read, gate = self._graph()
        with self.assertRaises(EvalFailureError) as caught:
            root.eval()
        failure = caught.exception
        self.assertEqual(failure.code_name, 'UNBOUND')
        self.assertIs(failure.node, gate)  # the visit whose evaluation refused
        self.assertIsNotNone(root.eval_path.failed)
        self.assertIs(failure.node, root.eval_path.failed)  # the same node, both ways
        self.assertNotEqual(failure.run_id, 0)  # a walk is a run
        self.assertEqual(failure.stages, ('pre',))
        self.show('a walk that stopped', failure)

    def test_01_the_failing_node_is_the_one_asked_why(self) -> None:
        """The record and the failure name the same node, so both lead there."""
        root, read, gate = self._graph()
        with self.assertRaises(EvalFailureError):
            root.eval()
        self.assertIs(root.eval_path.failed, gate)
        self.assertEqual(gate.type, 'BINARY')

    def test_02_a_failure_inside_a_walk_does_not_own_the_record(self) -> None:
        root, read, gate = self._graph()
        with self.assertRaises(EvalFailureError):
            root.eval()
        self.assertEqual(root.eval_path.code_name, 'UNBOUND')
        self.assertIsNone(root.eval_path.leaf)


if __name__ == '__main__':
    unittest.main()
