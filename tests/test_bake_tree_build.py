"""Tree building over a mapping: the store and the graph together.

The other bake suites test one module each. This one builds graphs the way a
caller would, so what is checked is the INTERACTION: a store read used as an
operand, a root entered inside a mapping, a scope that is not a store, and a
store filled in after the graph that reads it was written.

That last one is the whole point of the reservation protocol: the graph is
written first and the values arrive afterwards, in the entries its reads already
point at.

Oracle: the values written to the store, read back through the graph built over
them, and the C layer's own reported type of every node built.
"""

import itertools
import unittest

# Store names are short: they end up in every read's display text.
_store_tag = 'mt'
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
from decision_graph.decision_tree.bake.c_expr import (
    BinaryExpression,
    CallExpression,
    ExpressionOperator,
    TernaryExpression,
    UnaryExpression,
)
from decision_graph.decision_tree.bake.c_hierarchy import BreakpointNode, RootLogicNode
from decision_graph.decision_tree.bake.c_logic_group import LGM, LogicGroup
from decision_graph.decision_tree.bake.c_node import NODE_REGISTRY, LogicNode, PlaceholderNode



class BuildTestCase(unittest.TestCase):
    """A base that gives each test a store of its own (names are process-wide)."""

    def setUp(self) -> None:
        # Short, because a store's name is part of every read's display text and
        # the layer refuses one long enough to overflow the repr buffer.
        self.scope = f'{_store_tag}{next(_store_counter)}'
        self.mapping = LogicMapping(name=self.scope)


class TestReadingAStore(BuildTestCase):
    """A store read is an operand, and the store is what a build runs inside."""

    def test_00_a_graph_is_written_before_the_values_arrive(self) -> None:
        """The reservation protocol end to end: build first, fill later."""
        with self.mapping:
            close = self.mapping['close']
            open_ = self.mapping['open']
            spread = close - open_

        self.assertEqual(spread.op, ExpressionOperator.sub)
        self.assertIsNone(close.value)  # nothing written yet, and no error
        self.assertIsNone(open_.value)

        self.mapping['close'] = 10.5
        self.mapping['open'] = 8.0
        self.assertEqual(close.value, 10.5)
        self.assertEqual(open_.value, 8.0)

    def test_01_a_read_keys_itself_into_the_store(self) -> None:
        """Every operand that names an entry is an entry the store now holds."""
        with self.mapping:
            self.mapping['a']
            self.mapping['b']
            self.mapping['a']  # reserved again, not a second entry
        self.assertEqual(len(self.mapping), 2)

    def test_02_a_read_may_be_operated_on_by_every_binary_operator(self) -> None:
        """The operators over a read are the operators over any node."""
        with self.mapping:
            read = self.mapping['x']
            nodes = (
                (read + ConstantNode(1), ExpressionOperator.add),
                (read - ConstantNode(1), ExpressionOperator.sub),
                (read * ConstantNode(1), ExpressionOperator.mul),
                (read / ConstantNode(1), ExpressionOperator.div),
                (read // ConstantNode(1), ExpressionOperator.floordiv),
                (read ** ConstantNode(1), ExpressionOperator.pow),
                (read < ConstantNode(1), ExpressionOperator.lt),
                (read <= ConstantNode(1), ExpressionOperator.le),
                (read > ConstantNode(1), ExpressionOperator.gt),
                (read >= ConstantNode(1), ExpressionOperator.ge),
                (read & ConstantNode(True), ExpressionOperator.and_),
                (read | ConstantNode(True), ExpressionOperator.or_),
            )
        for node, member in nodes:
            with self.subTest(operator=member.name):
                self.assertIsInstance(node, BinaryExpression)
                self.assertEqual(node.op, member)

    def test_03_a_read_may_be_negated_and_inverted(self) -> None:
        """And the unary operators take the one operand they need."""
        with self.mapping:
            read = self.mapping['x']
            for node, member in ((-read, ExpressionOperator.neg), (~read, ExpressionOperator.not_)):
                with self.subTest(operator=member.name):
                    self.assertIsInstance(node, UnaryExpression)
                    self.assertEqual(node.op, member)

    def test_04_reads_and_literals_mix_in_one_expression(self) -> None:
        """A graph does not care where an operand's value comes from."""
        with self.mapping:
            read = self.mapping['store_value']
            both = read + ConstantNode(1)
        self.mapping['store_value'] = 41
        self.assertEqual(both.op, ExpressionOperator.add)
        self.assertEqual(read.value + 1, 42)

    def test_05_a_variable_node_and_a_store_read_are_the_same_kind(self) -> None:
        """Both are variable nodes: one bound by hand, one bound by the store."""
        variable = VariableNode(key='n')
        variable.c_bind_const(ConstantNode(9))
        with self.mapping:
            read = self.mapping['n']
        self.assertIsInstance(variable, VariableNode)
        self.assertIsInstance(read, VariableNode)
        self.assertEqual(variable.type, read.type)
        self.assertEqual(variable.value, 9)

    def test_06_a_read_of_a_reserved_entry_reads_it_once_a_value_arrives(self) -> None:
        """The type arrives with the value, and the read follows it - once.

        The FIRST value is what types the entry: an entry keeps its type
        (DCG_MAPPING_IMMUTABLE_DTYPE), so what a read reports from then on is that
        type's values, and another type is refused rather than taken.
        """
        with self.mapping:
            read = self.mapping['typed']
        self.assertIsNone(read.value)

        value = 7
        self.mapping['typed'] = value
        self.assertEqual(read.value, value)

        self.mapping['typed'] = 8
        self.assertEqual(read.value, 8)

        with self.assertRaises(RuntimeError):
            self.mapping['typed'] = 2.5  # a double into an int entry


class TestScopesOverAStore(BuildTestCase):
    """The mapping is a scope, and a graph has scopes of its own inside it."""

    def test_00_a_root_opens_a_context_of_its_own(self) -> None:
        """Entering a root shelves the build's scopes, the store included.

        A root is a graph entry point, not a store: once it is entered, there is
        no active group to read - which is the shelving doing what it is for.
        """
        with self.mapping:
            self.assertIs(LGM.active_group, self.mapping)
            with RootLogicNode(name='Entry'):
                self.assertIsNone(LGM.active_group)

    def test_01_a_read_outside_the_store_it_was_built_in_is_refused(self) -> None:
        """Once a root has shelved the build, no read can be built."""
        with self.mapping:
            self.mapping['close']  # fine: the mapping is the scope
            with RootLogicNode(name='Entry'):
                with self.assertRaises(RuntimeError):
                    AttrExpression('open')

    def test_02_a_read_in_a_plain_group_is_refused(self) -> None:
        """Only a store has entries to read."""
        group = LogicGroup(name=f'{self.scope}.plain')
        with group:
            with self.assertRaises(TypeError):
                AttrExpression('close')

    def test_03_the_store_survives_the_scopes_built_inside_it(self) -> None:
        """Shelving puts the build's stacks away; it does not touch the store."""
        with self.mapping:
            read = self.mapping['close']
            with RootLogicNode(name='Entry'):
                pass
        self.mapping['close'] = 3.0
        self.assertEqual(read.value, 3.0)

    def test_04_a_second_graph_over_the_same_store_shares_its_entries(self) -> None:
        """Two graphs built in one mapping read the same entry."""
        with self.mapping:
            left = self.mapping['shared']
        with self.mapping:
            right = self.mapping['shared']
        self.mapping['shared'] = 7.0
        self.assertEqual(left.value, 7.0)
        self.assertEqual(right.value, 7.0)
        self.assertEqual(len(self.mapping), 1)  # one entry, two reads


class TestGraphsOverAStore(BuildTestCase):
    """Whole trees: a store read hung under real nodes of every family."""

    def test_00_a_graph_is_rooted_in_the_store(self) -> None:
        """A decision graph: a root, a branch condition, an action at the end."""
        with self.mapping:
            signal = self.mapping['signal']
            root = RootLogicNode(name='Entry')
            with root:
                with signal > ConstantNode(0):
                    leaf = LongAction()

        self.assertEqual(root.type, 'ROOT')
        self.assertEqual(leaf.type, 'LONGACTION')

        # The condition took the root's arm, and the leaf hangs under it. The arm
        # the build did not fill was auto-filled, which is the layer's own rule:
        # a branch with nothing in it is an empty branch, not a missing one.
        condition = root.children[list(root.children)[0]]
        self.assertEqual(condition.type, 'BINARY')
        self.assertEqual(condition.op, ExpressionOperator.gt)

        arms = [node.type for node in condition.children.values()]
        self.assertIn('LONGACTION', arms)
        self.assertEqual(len(arms), 2)

    def test_01_the_graph_is_a_real_tree(self) -> None:
        """What was built is a graph the layer can walk."""
        with self.mapping:
            read = self.mapping['x']
            root = RootLogicNode(name='Entry')
            branch = BinaryExpression(ExpressionOperator.gt, read, ConstantNode(0))
            root.append(branch)
        self.assertEqual(root.size, 2)  # the root and the branch under it
        self.assertEqual(branch.parent.address, root.address)

    def test_02_the_store_still_feeds_the_graph_after_it_is_built(self) -> None:
        """The read is live: the value lands after the graph was written."""
        with self.mapping:
            signal = self.mapping['signal']
            root = RootLogicNode(name='Entry')
            with root:
                with signal > ConstantNode(0):
                    leaf = LongAction()
        self.mapping['signal'] = -1.0
        self.assertEqual(signal.value, -1.0)
        self.assertEqual(leaf.type, 'LONGACTION')

    def test_03_a_frozen_store_still_feeds_a_graph_over_it(self) -> None:
        """Sealing closes the store to new entries, not to the graph reading it."""
        self.mapping['close'] = 12.0
        self.mapping.frozen = True

        with self.mapping:
            read = self.mapping['close']
            total = read + ConstantNode(1)

        self.assertEqual(total.op, ExpressionOperator.add)
        self.assertEqual(read.value, 12.0)

        with self.mapping:
            with self.assertRaises(KeyError):
                self.mapping['brand_new']

    def test_04_a_breakpoint_names_the_group_it_breaks_out_of(self) -> None:
        """A break names the scope it leaves, and holds it."""
        sink = BreakpointNode(break_from=self.mapping)
        self.assertEqual(sink.type, 'BREAKPOINT')
        self.assertIs(sink.break_from, self.mapping)


class TestNodeFamiliesTogether(BuildTestCase):
    """Every family in one build, so the interactions are exercised together."""

    def test_00_a_full_build_holds_every_family(self) -> None:
        """A store, a literal, an expression, a root, an action and a placeholder."""
        with self.mapping:
            read = self.mapping['close']
            literal = ConstantNode(2)
            condition = read > literal
            root = RootLogicNode(name='Entry')
            leaf = LongAction()
            arm = PlaceholderNode()

        built = (read, literal, condition, root, leaf, arm)
        self.assertEqual(
            [type(node).__name__ for node in built],
            ['AttrExpression', 'ConstantNode', 'BinaryExpression', 'RootLogicNode', 'LongAction', 'PlaceholderNode'],
        )
        for node in built:
            with self.subTest(node=type(node).__name__):
                self.assertIsInstance(node, LogicNode)
                self.assertIn(node.address, NODE_REGISTRY)

    def test_01_a_leaf_ends_a_branch(self) -> None:
        """An action is where a branch stops: it is a leaf of the tree."""
        with self.mapping:
            root = RootLogicNode(name='Entry')
            with root:
                leaf = ClearAction()
        self.assertTrue(leaf.is_leaf)
        self.assertEqual(leaf.type, 'CLEARACTION')

    def test_02_branches_are_written_from_the_root(self) -> None:
        """Two graphs over one store, each rooted in its own entry point."""
        with self.mapping:
            signal = self.mapping['signal']
            up = RootLogicNode(name=f'{self.scope}.up')
            with up:
                with signal > ConstantNode(0):
                    NoAction()
            down = RootLogicNode(name=f'{self.scope}.down')
            with down:
                with signal < ConstantNode(0):
                    ShortAction()

        self.mapping['signal'] = 1.0
        self.assertGreaterEqual(up.size, 2)
        self.assertGreaterEqual(down.size, 2)


class TestDeepTrees(BuildTestCase):
    """Whole decision trees, built with the with-statement, printed as they are.

    Each test builds one tree and prints its layout, so a tree can be read on its
    own rather than inferred from the assertions.

    Four rules of the layer shape these trees, all of them its own:

      - the ROOT comes first and the store inside it. A root shelves the build's
        contexts when it is entered, so a store entered outside it is shelved
        away before any read can be built;
      - a root takes ONE branch. Its arm is the entry point's single edge, so the
        whole tree hangs under one condition entered at the top;
      - the arms of a branching node DO take siblings, one branch each;
      - comparisons are written as nodes - ``BinaryExpression(ExpressionOperator.eq,
        ...)`` - rather than with ``==``, because a node's ``__eq__`` answers with a
        bool: that is what ``==`` means in Python, so the operator form is the one
        that builds a branch.

    The store's name is short on purpose: it is part of every read's display text,
    and a long one overflows the buffer the C layer composes a repr in.
    """

    def setUp(self) -> None:
        super().setUp()
        self.store = LogicMapping(name=f'store{next(_store_counter)}')

    def test_00_a_four_level_decision_tree(self) -> None:
        """A trading decision, four levels deep, over a store of six entries."""
        with RootLogicNode(name='Entry') as root:
            with self.store:
                flat = BinaryExpression(ExpressionOperator.eq, self.store['exposure'], ConstantNode(0))
                with flat:
                    with self.store['volatility'] > ConstantNode(0.25):
                        with self.store['down_prob'] > ConstantNode(0.2):
                            with self.store['ttl'] > ConstantNode(30):
                                LongAction()
                            ShortAction()
                        with self.store['up_prob'] < ConstantNode(-0.1):
                            CancelAction()
                    with self.store['exposure'] > ConstantNode(0):
                        ClearAction()

        self.assertEqual(root.type, 'ROOT')
        self.assertGreaterEqual(root.size, 7)
        print(root.render())

    def test_01_every_leaf_type_in_one_tree(self) -> None:
        """Nested branches reaching each of the five action leaves."""
        with RootLogicNode(name='Entry') as root:
            with self.store:
                signal = self.store['signal']
                # A branching node has as many children as it has arms, so the
                # tree widens by nesting rather than by piling siblings up.
                with signal > ConstantNode(1):
                    with signal > ConstantNode(2):
                        with signal > ConstantNode(3):
                            LongAction()
                        ShortAction()
                    with signal < ConstantNode(-1):
                        flat = BinaryExpression(ExpressionOperator.eq, signal, ConstantNode(0))
                        with flat:
                            moved = BinaryExpression(ExpressionOperator.ne, signal, ConstantNode(0))
                            with moved:
                                NoAction()
                            CancelAction()
                        ClearAction()

        print(root.render())
        self.assertGreaterEqual(root.size, 7)

    def test_02_expressions_of_every_arity_in_one_tree(self) -> None:
        """Branches decided by a unary, a binary, a ternary and a call."""
        with RootLogicNode(name='Entry') as root:
            with self.store:
                close = self.store['close']
                open_ = self.store['open']
                high = self.store['high']

                unary = -close
                binary = close - open_
                ternary = TernaryExpression(ExpressionOperator.none, high > close, close, open_)
                call = CallExpression(ExpressionOperator.none, [close, open_, high], name='spread')

                with unary > ConstantNode(0):
                    with binary > ConstantNode(0):
                        with ternary > ConstantNode(0):
                            with call > ConstantNode(0):
                                LongAction()
                            ShortAction()
                        NoAction()

        print(root.render())
        for node, expected in ((unary, 'UNARY'), (binary, 'BINARY'), (ternary, 'TERNARY'), (call, 'CALL')):
            with self.subTest(expression=expected):
                self.assertEqual(node.type, expected)
        self.assertEqual(ternary.n_args, 3)
        self.assertEqual(call.n_args, 3)

    def test_03_a_tree_that_mixes_actions_branches_and_reads(self) -> None:
        """Actions and branching nodes side by side, which is what a real graph is."""
        with RootLogicNode(name='Entry') as root:
            with self.store:
                exposure = self.store['exposure']
                with exposure > ConstantNode(0):
                    with exposure > ConstantNode(1):
                        LongAction()
                    with exposure <= ConstantNode(0):
                        ShortAction()

        print(root.render(show_labels=True, show_out=True))
        self.assertGreaterEqual(root.size, 4)

    def test_04_labels_and_validation_on_a_deep_tree(self) -> None:
        """A tree the layer reports as well-formed, with its branches named."""
        with RootLogicNode(name='Entry') as root:
            with self.store:
                signal = self.store['signal']
                with signal > ConstantNode(0):
                    with signal > ConstantNode(1):
                        branch = LongAction()
                        branch.label('long branch')
                    ShortAction()

        print(root.render(show_labels=True))
        self.assertIsNone(root.validate())  # no report: the tree is sound
        self.assertEqual(branch.labels, ['long branch'])
        self.assertTrue(branch.has_label('long branch'))


class TestAgainstCapi(BuildTestCase):
    """The same tree, built by both layers, compared by its layout.

    The two layers are the same grammar over different backends, so the same code
    should mean the same decision. What is compared is the LAYOUT - what branches,
    what hangs off each branch, where the leaves are.

    The differences this finds, asserted as they stand rather than papered over:

      - the arms of a node are stored in OPPOSITE orders: bake keeps TRUE first -
        the arm a sequential check reads first, and the one its render shows
        above the fallback - while the capi keeps the FALSE arm first. The fill
        gives the first branch built the TRUE arm in both;
      - so a walk meets two SIBLING branches in opposite orders: bake reaches
        the true branch first, the capi the fallback.

    Both layouts are printed, so a difference can be read rather than guessed at.
    """

    def setUp(self) -> None:
        super().setUp()
        self.store = LogicMapping(name=f'store{next(_store_counter)}')

    def _leaves(self, node) -> list:
        """The leaves of a tree, left to right, by the TYPE each one is.

        The C layer's type, not the wrapper's class: an arm a build leaves is
        closed by the C layer as an auto no-action, and the wrapper the layer
        still holds for the arm it grew is the class the node WAS - the layer has
        no event that refreshes a wrapper's class yet. The TYPE is the truth both
        sides agree on, and it is what this walk is asking about.
        """
        if not node.children:
            return [self._leaf_name(node)]
        out = []
        for child in node.children.values():
            out.extend(self._leaves(child))
        return out

    def _leaf_name(self, node) -> str:
        """A leaf's name in the one vocabulary both layers have: the C type's.

        bake reports the node's type; the capi has no such property, so its class
        name stands in - the same word the type spells.
        """
        return node.type if hasattr(node, 'type') else type(node).__name__.upper()

    def _actions(self, node) -> list:
        """The ACTION leaves of a tree: what each arm decides, ignoring the two
        layers' own filler (a placeholder, an auto-connected no-action)."""
        return [leaf for leaf in self._leaves(node) if leaf not in ('PLACEHOLDER', 'NOACTION')]

    def _capi_demo_tree(self):
        """The capi's own demo tree: two branches under one condition."""
        from decision_graph.decision_tree import LogicMapping as CapiMapping
        from decision_graph.decision_tree import LongAction as CapiLong
        from decision_graph.decision_tree import RootLogicNode as CapiRoot
        from decision_graph.decision_tree import ShortAction as CapiShort

        root = CapiRoot()
        with root:
            with CapiMapping(name=f'capi{next(_store_counter)}', data={'volatility': 0.24, 'down_prob': 0.2, 'up_prob': 0.8}) as store:
                with store.volatility > 0.25:
                    with store.down_prob > 0.2:
                        CapiLong()
                    with store.up_prob < -0.1:
                        CapiShort()
        return root

    def _bake_demo_tree(self):
        """The same shape in bake, with the reads bound to a store."""
        with RootLogicNode(name='Entry') as root:
            with self.store:
                with self.store['volatility'] > ConstantNode(0.25):
                    with self.store['down_prob'] > ConstantNode(0.2):
                        LongAction()
                    with self.store['up_prob'] < ConstantNode(-0.1):
                        ShortAction()
        return root

    def test_00_the_capi_demo_tree_in_both_layers(self) -> None:
        """The demo shape, built once per layer, compared by what it decides."""
        capi_root = self._capi_demo_tree()
        bake_root = self._bake_demo_tree()

        print('capi leaves:', self._leaves(capi_root))
        print('bake leaves:', self._leaves(bake_root))
        print(bake_root.render())

        # The same two decisions are reachable, in the same nesting: the order is
        # the difference the next test pins.
        self.assertEqual(sorted(self._actions(bake_root)), sorted(self._actions(capi_root)))
        self.assertEqual(sorted(self._actions(bake_root)), ['LONGACTION', 'SHORTACTION'])

    def test_01_the_sibling_order_differs(self) -> None:
        """Two sibling branches are walked in opposite orders.

        Deliberate, and it follows from the arm order: bake stores a node's arms
        TRUE-first - the order a sequential check reads them - while the capi
        keeps the FALSE arm first. The fill gives the first branch built the
        TRUE arm in both, so the two trees hold the same branches in opposite
        lists.
        """
        capi_actions = self._actions(self._capi_demo_tree())
        bake_actions = self._actions(self._bake_demo_tree())

        self.assertEqual(capi_actions, ['SHORTACTION', 'LONGACTION'])
        self.assertEqual(bake_actions, ['LONGACTION', 'SHORTACTION'])

    def test_02_both_layers_fill_the_arms_they_did_not_use(self) -> None:
        """Every spare arm is an auto-connected NoAction, in both layers.

        Neither layer leaves a reservation standing: an arm a build did not fill
        is closed with an auto no-action when the node is exited, and the
        wrapper is told, so what the Python side reports and what the C tree
        holds agree.
        """
        capi_leaves = self._leaves(self._capi_demo_tree())
        bake_leaves = self._leaves(self._bake_demo_tree())

        print('capi leaves:', capi_leaves)
        print('bake leaves:', bake_leaves)

        self.assertEqual(sorted(capi_leaves), sorted(bake_leaves))
        self.assertEqual(sorted(bake_leaves), ['LONGACTION', 'NOACTION', 'NOACTION', 'SHORTACTION'])
        self.assertNotIn('PLACEHOLDER', bake_leaves)  # no reservation is left standing
