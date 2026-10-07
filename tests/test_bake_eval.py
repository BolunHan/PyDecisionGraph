"""Evaluating a graph: the decision it reaches, and the hooks that shape it.

The other suites build graphs and check their shape. This one runs them: a value
in the store, a walk from the root down, and a single leaf of the whole tree
where the walk comes to rest.

The oracle is the decision itself. Every case here says which leaf the graph
should reach for a given set of values - and the leaves are asserted by their
TYPE, because an arm the build left standing is closed by the C layer with an
auto no-action, and the wrapper the registry still holds for it is the class the
node WAS: the layer has no event that refreshes a wrapper's class yet, so the
type the C node reports is the truth both sides agree on.

The hooks have a second oracle: the order the stages ran in. A hook that
records its own call is asked to produce the sequence, and the sequence is
compared against the protocol - pre, then the value, then post.
"""

import itertools
import os
import sys
import unittest

# Store names are short: they end up in every read's display text.
_store_tag = 'et'
_store_counter = itertools.count()

from decision_graph.decision_tree.bake.c_action import (
    CancelAction,
    ClearAction,
    LongAction,
    NoAction,
    ShortAction,
)
from decision_graph.decision_tree.bake.c_collections import AttrExpression, LogicMapping
from decision_graph.decision_tree.bake.c_const import ConstantNode, VariableNode
from decision_graph.decision_tree.bake.c_var import VarType
from decision_graph.decision_tree.bake.c_expr import (
    BinaryExpression,
    CallExpression,
    ExpressionOperator,
    TernaryExpression,
    UnaryExpression,
)
from decision_graph.decision_tree.bake.c_hierarchy import BreakpointNode, RootLogicNode
from decision_graph.decision_tree.bake.c_logic_group import LGM, LogicGroup
from decision_graph.decision_tree.bake.c_edge import NO_CONDITION
from decision_graph.decision_tree.bake.c_node import NODE_REGISTRY, LogicNode, PlaceholderNode
from decision_graph.decision_tree.exc import EvalFailureError

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from bake_eval_probe import (  # noqa: E402  (the Cython half: cdef hooks need a Cython subclass)
    BothHooksAction,
    HookedAction,
    RefusingAction,
    WatchingAction,
)


class EvalTestCase(unittest.TestCase):
    """A base that gives each test a store of its own (names are process-wide)."""

    def setUp(self) -> None:
        self.scope = f'{_store_tag}{next(_store_counter)}'
        self.mapping = LogicMapping(name=self.scope)

    def _signal_tree(self):
        """A decision tree over one entry, with its branch nodes.

        The shape every deciding test uses:

            root
             +-- signal > 0            [the root's one edge]
                  +-- signal > 1       [true]
                  |    +-- long        [true]
                  |    +-- cancel      [false]
                  +-- flat             [false]

        A signal above 1 reaches the long action, a signal between 0 and 1 the
        cancel action, and anything at or below 0 the flat one.

        Written the way the layer's two rules require: a root takes ONE child, so
        every branch hangs below the one it is given; and an action written
        without a condition takes the arm its enclosing node has left, so the
        three leaves are the two arms of the inner branch and the false arm of
        the outer one.
        """
        with RootLogicNode(name='Entry') as root:
            with self.mapping:
                signal = self.mapping['signal']
                outer = BinaryExpression(ExpressionOperator.gt, signal, ConstantNode(0))
                inner = BinaryExpression(ExpressionOperator.gt, signal, ConstantNode(1))
                with outer:
                    with inner:
                        long_ = LongAction()
                        cancel = CancelAction()
                    flat = ClearAction()
        return root, outer, inner, long_, cancel, flat


class TestDeciding(EvalTestCase):
    """A graph evaluated: which leaf it reaches, and what the walk recorded."""

    def test_00_a_tree_decides_by_the_values_it_reads(self) -> None:
        """Three signals, three leaves, the same graph each time."""
        root, outer, inner, long_, cancel, flat = self._signal_tree()

        cases = (
            (2.0, long_),
            (0.5, cancel),
            (-1.0, flat),
        )
        for value, expected in cases:
            with self.subTest(signal=value):
                self.mapping['signal'] = value
                landed = root.eval()
                self.assertIs(landed, expected)
                self.assertEqual(landed.type, expected.type)
                print(f'signal = {value:>5}  ->  {root.eval_path}')

    def test_00b_a_failure_names_the_node_the_stage_and_the_run(self) -> None:
        """What a failed evaluation reports: where it stopped, and how far it got.

        The failing node's own state is what says it - not the node the caller
        asked - because in a walk the node that reported the code is the one that
        refused, and the root that reached it is not.
        """
        root, outer, inner, long_, cancel, flat = self._signal_tree()
        with self.assertRaises(RuntimeError) as caught:
            root.eval()
        message = str(caught.exception)
        print('walk failure:', message)

        self.assertIn('BINARY', message)      # the branch, not the root
        self.assertIn(outer.repr, message)
        self.assertIn('UNBOUND (-18)', message)
        self.assertIn('stages completed: pre', message)  # it stopped before its value existed
        self.assertIn('run 0x', message)                 # and in a run, with a run id

        # The record says the same thing, in the fields a caller reads.
        path = root.eval_path
        self.assertEqual(path.code_name, 'UNBOUND')
        self.assertIs(path.failed, outer)
        self.assertIsNone(path.leaf)
        self.assertNotEqual(path.seq_id, 0)

        # A dry run of one node reports that node, and says there was no run.
        with self.mapping:
            unset = self.mapping['never_set']  # a read names its entry: it needs the store open
        with self.assertRaises(RuntimeError) as dry:
            unset.eval()
        print('dry failure:', dry.exception)
        self.assertIn('VARIABLE', str(dry.exception))
        self.assertIn('no run', str(dry.exception))

    def test_01_the_path_is_the_walk_not_just_the_leaf(self) -> None:
        """How the decision was reached, in order, with the wrappers the build made."""
        root, outer, inner, long_, cancel, flat = self._signal_tree()
        self.mapping['signal'] = 2.0

        root.eval()
        path = root.eval_path

        self.assertEqual(len(path), 4)
        self.assertEqual([node.type for node in path], ['ROOT', 'BINARY', 'BINARY', 'LONGACTION'])
        self.assertEqual([node.type for node in path.nodes], ['ROOT', 'BINARY', 'BINARY', 'LONGACTION'])
        self.assertIs(path[-1], path.leaf)              # the last entry is where it landed
        self.assertIs(path[3], long_)
        self.assertEqual(path.code_name, 'OK')
        self.assertGreater(path.capacity, 0)
        self.assertIsNotNone(path.address)
        print('record:', path)
        self.assertIs(path[0], root)
        self.assertIs(path[1], outer)
        self.assertIs(path[2], inner)
        self.assertIs(path[3], long_)

        # A walk that stops higher up records less, and touches nothing below it.
        self.mapping['signal'] = -1.0
        root.eval()
        self.assertEqual([node.type for node in root.eval_path], ['ROOT', 'BINARY', 'CLEARACTION'])
        self.assertIs(root.eval_path[-1], flat)
        self.assertIs(root.eval_path.leaf, flat)

    def test_02_a_read_resolves_when_its_value_arrives(self) -> None:
        """The deferred thread: a read that holds its entry's offset until it is read.

        The graph is written first and the value arrives afterwards, so the read
        begins as an offset the store could not type. Its FIRST evaluation is what
        resolves it - to the entry's type as of that moment - and from then on the
        slot holds the reference and every further evaluation is the fast path.
        """
        with self.mapping:
            signal = self.mapping['signal']

        # What the read holds before it is read is WHERE its entry is, and no type
        # of its own: an offset, tagged as the entry's type-to-be. Resolving it is
        # what an evaluation does.
        self.assertTrue(signal.out.is_null)
        self.assertEqual(signal.out.format(), '(reserved)')
        self.assertIs(signal.out.dtype, VarType.inferred)
        self.assertIs(signal.out.ref_base, VarType.reserved)

        with self.assertRaises(RuntimeError) as caught:
            signal.eval()
        self.assertIn('UNBOUND', str(caught.exception))

        # An evaluation that could not resolve anything changed nothing: the read
        # still holds the offset, and the store is still free to take entries.
        self.assertIs(signal.out.dtype, VarType.inferred)
        self.mapping['late'] = 1

        # What a read is resolved to is the entry's type at its first evaluation,
        # and one store can give one read that answer: resolving pins the read to
        # the entry it named.
        for value, expected in ((7, VarType.int), (2.5, VarType.double), (True, VarType.bool), ('text', VarType.string)):
            with self.subTest(value=value):
                store = LogicMapping(name=f'{_store_tag}{next(_store_counter)}')
                with store:
                    read = store['signal']
                store['signal'] = value

                self.assertEqual(read.eval(), value)  # as it went in, whatever it was
                self.assertTrue(read.out.is_ref)
                self.assertFalse(read.out.is_null)
                self.assertIs(read.out.ref_base, expected)  # the read names the entry's own type

                # A second evaluation resolves nothing again: the tag is a fact by
                # then, and the value behind it is still the entry's.
                self.assertEqual(read.eval(), value)
                self.assertIs(read.out.ref_base, expected)

    def test_03_a_store_that_cannot_answer_stops_the_walk(self) -> None:
        """A graph whose read was never filled reports where it stopped."""
        root, outer, inner, long_, cancel, flat = self._signal_tree()

        with self.assertRaises(RuntimeError) as caught:
            root.eval()
        self.assertIn('UNBOUND', str(caught.exception))

        # The record still holds the nodes the walk reached before it stopped.
        self.assertEqual([node.type for node in root.eval_path], ['ROOT', 'BINARY'])
        self.assertIsNone(root.eval_path.leaf)  # it never landed

    def test_04_an_operand_is_evaluated_before_it_is_read(self) -> None:
        """An expression's operand is a node, and a node has a value once it ran."""
        with self.mapping:
            close = self.mapping['close']
            open_ = self.mapping['open']
            spread = close - open_
            positive = spread > ConstantNode(0)

        self.mapping['close'] = 10.5
        self.mapping['open'] = 8.0
        self.assertIs(positive.eval(), True)
        self.assertEqual(spread.eval(), 2.5)

        self.mapping['open'] = 20.0
        self.assertIs(positive.eval(), False)

    def test_05_every_operator_evaluates_over_its_operands(self) -> None:
        """The operator family, applied the way Python applies it."""
        cases = (
            (ExpressionOperator.add, 1, 2, 3),
            (ExpressionOperator.sub, 1, 2, -1),
            (ExpressionOperator.mul, 3, 4, 12),
            (ExpressionOperator.div, 7, 2, 3.5),
            (ExpressionOperator.floordiv, 7, 2, 3),
            (ExpressionOperator.pow, 2, 10, 1024),
            (ExpressionOperator.eq, 2, 2, True),
            (ExpressionOperator.ne, 2, 3, True),
            (ExpressionOperator.gt, 3, 2, True),
            (ExpressionOperator.le, 3, 2, False),
            (ExpressionOperator.and_, 1, 5, 5),
            (ExpressionOperator.or_, 0, 5, 5),
        )
        for operator, x, y, expected in cases:
            with self.subTest(operator=operator.name):
                node = BinaryExpression(operator, ConstantNode(x), ConstantNode(y))
                self.assertEqual(node.eval(), expected)

    def test_06_a_ternary_reads_the_arm_its_condition_picks(self) -> None:
        node = TernaryExpression(
            ExpressionOperator.none,
            BinaryExpression(ExpressionOperator.gt, ConstantNode(2), ConstantNode(1)),
            ConstantNode(10),
            ConstantNode(20),
        )
        self.assertEqual(node.eval(), 10)

    def test_07_a_call_has_no_evaluation(self) -> None:
        """A callee is display text, not a node: there is nothing to apply."""
        with self.assertRaises(RuntimeError) as caught:
            CallExpression(ExpressionOperator.none, [ConstantNode(1)], name='spread').eval()
        self.assertIn('TYPE', str(caught.exception))

    def test_08_a_root_reports_the_leaf_and_a_plain_node_its_value(self) -> None:
        """What a door answers depends on what it walked.

        A root that lands on an action answers with the action - that is the
        decision. A root that lands where there is no action to hand back - a
        breakpoint that was never connected - answers with the value in the slot,
        which for an inspection point that produced nothing is None.
        """
        with RootLogicNode(name='Entry') as root:
            with self.mapping:
                sink = BreakpointNode(break_from=self.mapping)
                # A breakpoint does not join a graph as it is made (unlike an
                # action), so the entry's arm is taken by the breakpoint itself.
                root.overwrite(sink, NO_CONDITION)

        self.assertIsNone(root.eval())
        self.assertEqual([node.type for node in root.eval_path], ['ROOT', 'BREAKPOINT'])

    def test_09_a_single_node_evaluates_without_a_walk(self) -> None:
        """The dry run: one node, its own value, no bookkeeping."""
        literal = ConstantNode(2.5)
        self.assertEqual(literal.eval(), 2.5)

        branch = BinaryExpression(ExpressionOperator.gt, literal, ConstantNode(1))
        self.assertIs(branch.eval(), True)
        self.assertFalse(branch.out.is_null)

        # The value stays in the slot, and the node reports what it holds.
        self.assertEqual(branch.out.value, True)

    def test_10_an_unevaluable_operand_is_refused_rather_than_guessed(self) -> None:
        """A string has no arithmetic, and a zero divisor has no answer."""
        divided = BinaryExpression(ExpressionOperator.div, ConstantNode(1.0), ConstantNode(0.0))
        with self.assertRaises(RuntimeError) as caught:
            divided.eval()
        self.assertIn('MATH', str(caught.exception))

        texted = BinaryExpression(ExpressionOperator.add, ConstantNode('text'), ConstantNode(1))
        with self.assertRaises(RuntimeError) as caught:
            texted.eval()
        self.assertIn('MATH', str(caught.exception))


class TestPythonHooks(EvalTestCase):
    """The Python half of the hook protocol: a subclass overrides a method."""

    def test_00_a_python_hook_is_installed_and_runs_in_order(self) -> None:
        class Logging(NoAction):
            def __init__(self, **kwargs):
                self.calls = []
                super().__init__(**kwargs)

            def pre_eval_fn(self):
                self.calls.append('pre_eval')

            def eval_fn(self):
                self.calls.append('eval_fn')

            def post_eval_fn(self):
                self.calls.append('post_eval')

        node = Logging(repr='logging')

        self.assertEqual(node.eval_hooks, ('pre_eval_fn', 'eval_fn', 'post_eval_fn'))
        node.eval()
        self.assertEqual(node.calls, ['pre_eval', 'eval_fn', 'post_eval'])

        # And the same hooks run when the node is reached by a walk, not only by
        # the door that calls it directly. An action built inside a root's block
        # joins that root as it is made, so there is nothing to append.
        with RootLogicNode(name='Entry') as root:
            child = Logging(repr='in a tree')
        root.eval()
        self.assertEqual(child.calls, ['pre_eval', 'eval_fn', 'post_eval'])

    def test_01_a_hook_is_installed_only_where_one_is_overridden(self) -> None:
        class PreOnly(NoAction):
            def pre_eval_fn(self):
                pass

        self.assertEqual(PreOnly().eval_hooks, ('pre_eval_fn',))

        class PostOnly(NoAction):
            def post_eval_fn(self):
                pass

        self.assertEqual(PostOnly().eval_hooks, ('post_eval_fn',))

        # A node that overrides nothing installs nothing, so the evaluator has no
        # hook to call - the built-in rule is what produces its value.
        self.assertEqual(LongAction().eval_hooks, ())

    def test_02_a_hook_can_refuse_the_node(self) -> None:
        """A hook's return value is the code the node ends with."""
        class Refusing(NoAction):
            def pre_eval_fn(self):
                return -17  # DCG_ERR_MATH, returned rather than raised.

        node = Refusing()
        with self.assertRaises(RuntimeError) as caught:
            node.eval()
        self.assertIn('MATH', str(caught.exception))

    def test_03_a_hook_that_raises_keeps_its_exception_as_the_cause(self) -> None:
        """A hook's failure is the hook's to describe, and the report carries it.

        What an evaluation raises is the layer's own failure - the node, the code,
        the stage, what was running - and the hook's exception travels as its
        CAUSE rather than in place of it, so nothing the hook said is lost and the
        node still ends with a code the protocol can name.
        """
        class Broken(NoAction):
            def pre_eval_fn(self):
                raise ValueError('the hook said no')

        node = Broken()
        self.assertEqual(node.eval_hooks, ('pre_eval_fn',))
        with self.assertRaises(EvalFailureError) as caught:
            node.eval()
        self.assertEqual(caught.exception.code_name, 'HOOK')
        self.assertEqual(caught.exception.source, 'pre_hook')
        self.assertEqual(caught.exception.failed_at, 'pre')
        self.assertIsInstance(caught.exception.__cause__, ValueError)
        self.assertEqual(str(caught.exception.__cause__), 'the hook said no')

        # The failure does not stick: the next evaluation asks the hook again.
        with self.assertRaises(EvalFailureError):
            node.eval()

    def test_04_a_wrapper_that_owns_nothing_does_not_inject(self) -> None:
        """A read built by a store is the store's, so it cannot hold a hook.

        The C node would end up pointing at a Python object nothing keeps alive,
        so the injection is refused - and said so on stderr rather than dropped.
        The hook is therefore not installed, and the built-in rule answers.
        """
        class HookedRead(AttrExpression):
            def pre_eval_fn(self):
                pass

        with self.mapping:
            read = HookedRead('close')

        self.assertIsInstance(read, AttrExpression)
        self.assertEqual(read.eval_hooks, ())  # nothing installed: the wrapper owns nothing

        self.mapping['close'] = 3.0
        self.assertEqual(read.eval(), 3.0)  # the read still answers, by its own rule


class TestCythonHooks(EvalTestCase):
    """The other half of the protocol: a Cython subclass overrides a cdef hook.

    Only Cython can define these, so the subclasses live in ``bake_eval_probe``.
    What is checked here is the whole chain: the override is DETECTED, the right
    adaptor is installed for the hook that was overridden, and calling it reaches
    the subclass's own code.
    """

    def test_00_a_cython_eval_hook_produces_the_value(self) -> None:
        node = HookedAction(repr='hooked')

        self.assertEqual(node.eval_hooks, ('c_eval_fn',))
        self.assertEqual(node.eval(), 41)  # the hook's value, not the type's
        self.assertEqual(node.calls, ['c_eval_fn'])

    def test_01_a_cython_hook_can_refuse_the_node(self) -> None:
        node = RefusingAction(repr='refusing')

        self.assertEqual(node.eval_hooks, ('c_pre_eval_fn',))
        with self.assertRaises(RuntimeError) as caught:
            node.eval()
        self.assertIn('BUSY', str(caught.exception))
        self.assertEqual(node.calls, ['c_pre_eval_fn'])

    def test_02_the_cython_hook_runs_before_the_python_one(self) -> None:
        """Both halves of every hook on one node, in the order the protocol runs."""
        node = BothHooksAction(repr='both')

        self.assertEqual(
            node.eval_hooks,
            ('c_pre_eval_fn', 'pre_eval_fn', 'c_eval_fn', 'eval_fn', 'c_post_eval_fn', 'post_eval_fn'),
        )
        self.assertEqual(node.eval(), 3)
        self.assertEqual(
            node.calls,
            ['c_pre_eval_fn', 'pre_eval_fn', 'c_eval_fn', 'eval_fn', 'c_post_eval_fn', 'post_eval_fn'],
        )

    def test_03_the_value_is_in_the_slot_by_the_post_stage(self) -> None:
        """The pre hook is where a refusal stops the node; by post, the value is there."""
        node = WatchingAction(repr='watching')

        self.assertEqual(node.eval_hooks, ('c_eval_fn', 'c_post_eval_fn'))
        node.eval()
        self.assertEqual(node.calls, ['c_eval_fn', 'c_post_eval_fn'])
        self.assertEqual(node.seen, 11)

    def test_04_a_cython_hook_runs_when_the_node_is_reached_by_a_walk(self) -> None:
        with RootLogicNode(name='Entry') as root:
            with self.mapping:
                with self.mapping['signal'] > ConstantNode(0):
                    leaf = HookedAction(repr='hooked in a tree')

        self.mapping['signal'] = 1.0
        # The hook WROTE the slot, so what the walk hands back is that value: a
        # leaf whose slot still stands for itself is what comes back as its own
        # wrapper (see the action cases), and this one holds 41 instead.
        self.assertEqual(root.eval(), 41)
        self.assertEqual(leaf.calls, ['c_eval_fn'])
        self.assertEqual(leaf.out.value, 41)  # the hook's value is what the slot holds


class TestAgainstCapi(EvalTestCase):
    """The same decision, reached by both layers over the same store.

    The two layers are the same grammar over different backends, so the same
    values should decide the same way. What is compared is the DECISION - which
    action each layer comes to rest on - for a set of values both stores can be
    given.
    """

    def _capi_decision(self, volatility: float):
        from decision_graph.decision_tree import LogicMapping as CapiMapping
        from decision_graph.decision_tree import LongAction as CapiLong
        from decision_graph.decision_tree import RootLogicNode as CapiRoot
        from decision_graph.decision_tree import ShortAction as CapiShort

        root = CapiRoot()
        store = CapiMapping(name=f'capi_eval{next(_store_counter)}', data={'volatility': volatility})
        with root:
            with store:
                with store.volatility > 0.25:
                    CapiLong()
                    with store.volatility < -0.1:
                        CapiShort()
        root()  # the capi's own door: evaluate and return the value
        return [type(node).__name__ for node in root.eval_path if isinstance(node, CapiLong | CapiShort)]

    def _bake_decision(self, volatility: float):
        with RootLogicNode(name='Entry') as root:
            with self.mapping:
                read = self.mapping['volatility']
                up = BinaryExpression(ExpressionOperator.gt, read, ConstantNode(0.25))
                with up:
                    LongAction()
                    down = BinaryExpression(ExpressionOperator.lt, read, ConstantNode(-0.1))
                    with down:
                        ShortAction()

        self.mapping['volatility'] = volatility
        root.eval()
        return [node.type for node in root.eval_path if node.type not in ('ROOT', 'BINARY')]

    def test_00_both_layers_reach_the_same_decision(self) -> None:
        for volatility, expected in ((0.5, 'LongAction'), (-0.5, 'ShortAction')):
            with self.subTest(volatility=volatility):
                capi = self._capi_decision(volatility)
                bake = self._bake_decision(volatility)
                print(f'volatility = {volatility:>5}  capi={capi}  bake={bake}')
                self.assertIn(expected, capi)
                self.assertIn(expected.upper(), bake)
