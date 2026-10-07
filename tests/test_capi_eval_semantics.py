"""What a root's ``eval`` is in each layer - and what a root's CALL is.

The two layers spell their doors differently, and one of those differences is not
a matter of taste: ``RootLogicNode.eval`` means something else in each.

capi's root carries an ENTRY EXPRESSION - the literal ``True`` its constructor
gives it - and its ``eval`` answers with that. It is what a skippable with-block
asks before it runs its body (``c_entry_check``), which is why a capi root's body
always runs. The DECISION is reached by asking the root: ``root()``.

bake's root has no entry expression to answer with, because bake has no skippable
with-block to ask one - so *its* ``eval`` is the walk, and ``__call__`` is that
same walk written as a call. A call is also what capi's decision is, so **the
call is the door the two layers agree on**, and ``eval`` is the one they do not.

This file pins all of it, measured. The point is not that the two ``eval``\\ s
differ - it is that the difference is deliberate and on the record, so the next
reader does not "align" them by making bake's root answer ``True``.
"""

import itertools
import unittest

from decision_graph.decision_tree.capi import LGM, c_abc, c_collection, c_node
from decision_graph.decision_tree.bake.c_action import LongAction, ShortAction
from decision_graph.decision_tree.bake.c_collections import LogicMapping
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode

# Bake store names are short: they end up in every read's display text.
_store_tag = 'es'
_store_counter = itertools.count()


class CapiRootCase(unittest.TestCase):
    """A capi tree of the shape a decision is made with, and its doors."""

    def setUp(self) -> None:
        LGM.clear()
        self.mapping = c_collection.LogicMapping(name='eval_sem', data={'a': 5})
        self.mapping.__enter__()

        with c_node.RootLogicNode() as root:
            with c_abc.LogicNode(expression=self.mapping['a'] > 3) as guard:
                self.long = c_abc.LongAction()
                self.short = c_abc.ShortAction()

        self.root = root
        self.guard = guard

    def tearDown(self) -> None:
        self.mapping.__exit__(None, None, None)
        LGM.clear()


class TestWhatTheRootsEvalIs(CapiRootCase):
    """``eval`` on a capi root answers the ENTRY EXPRESSION, never the decision."""

    def test_00_the_root_carries_an_entry_expression(self) -> None:
        """The literal ``True``, with ``dtype`` bool - the with-block's entry check."""
        self.assertIs(self.root.expression, True)
        self.assertIs(self.root.dtype, bool)

    def test_01_eval_answers_with_that_expression(self) -> None:
        """``eval`` is the entry check: it says the root may be entered."""
        self.assertIs(self.root.eval(), True)

    def test_02_eval_is_not_the_decision(self) -> None:
        """The walk would land on an action; ``eval`` does not walk at all."""
        decision = self.root.__call__()
        self.assertIsNot(self.root.eval(), decision)
        self.assertIsInstance(decision, c_abc.LongAction)

    def test_03_the_call_is_what_walks(self) -> None:
        """``root()`` reaches the leaf, and the record says which nodes it crossed."""
        self.root.__call__()
        crossed = [type(node).__name__ for node in self.root.eval_path]
        self.assertEqual(crossed, ['RootLogicNode', 'LogicNode', 'LongAction'])

    def test_04_a_non_root_answers_for_its_own_expression(self) -> None:
        """The guard's ``eval`` is its comparison's value; its call is the decision."""
        self.assertIs(self.guard.eval(), True)  # 5 > 3
        self.assertIsInstance(self.guard.__call__(), c_abc.LongAction)

    def test_05_a_leaf_answers_with_itself_either_way(self) -> None:
        """An action IS what it decides, so its value and its decision are one."""
        self.assertIsInstance(self.long.eval(), c_abc.LongAction)
        self.assertIsInstance(self.long.__call__(), c_abc.LongAction)

    def test_06_dry_run_answers_nothing_and_decides_nothing(self) -> None:
        """``dry_run`` evaluates each non-action node in place and returns None."""
        self.assertIsNone(self.root.dry_run())


class BakeRootCase(unittest.TestCase):
    """A bake tree of the same shape, so the two calls can be compared."""

    def setUp(self) -> None:
        self.mapping = LogicMapping(name=f'{_store_tag}{next(_store_counter)}')

        with RootLogicNode(name='Entry') as root:
            with self.mapping:
                self.signal = self.mapping['signal']
                guard = BinaryExpression(ExpressionOperator.gt, self.signal, ConstantNode(0))
                with guard:
                    self.long = LongAction()
                    self.short = ShortAction()

        self.root = root
        self.guard = guard

    def tearDown(self) -> None:
        self.mapping.clear()


class TestWhatTheBakeCallIs(BakeRootCase):
    """bake's root has no entry expression - its ``eval`` IS the walk."""

    def test_00_the_call_and_the_eval_are_the_same_door(self) -> None:
        """Both answer the decision, for the same graph, with the same record."""
        with self.mapping:
            self.mapping['signal'] = 1
        self.assertIsInstance(self.root(), LongAction)
        self.assertIsInstance(self.root.eval(), LongAction)

    def test_01_the_call_writes_the_same_record_the_eval_does(self) -> None:
        """The call is the eval, so what a caller reads afterwards is one walk."""
        with self.mapping:
            self.mapping['signal'] = 1
        via_call = [type(node).__name__ for node in (self.root(), self.root.eval_path)[1]]

        via_eval = [type(node).__name__ for node in (self.root.eval(), self.root.eval_path)[1]]
        self.assertEqual(via_call, via_eval)
        self.assertEqual(via_call[-1], 'LongAction')

    def test_02_a_failed_call_raises_what_the_eval_raises(self) -> None:
        """The door is shared, so the failure is too - here, a read with no value."""
        from decision_graph.decision_tree.exc import EvalFailureError

        with self.assertRaises(EvalFailureError):
            self.root()
