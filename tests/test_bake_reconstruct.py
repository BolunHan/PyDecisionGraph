"""Wrapper tests for ``decision_graph.decision_tree.bake.c_reconstruct``.

Scope is the way back: a graph that lives in C, reached from an address, becoming
the wrappers Python holds. Two halves, because the module has two:

  - **Restoration, in this process.** A reconstruction has to bring back more than
    the block: the children and their subtrees with the edges they hang by, the
    parent and through it the rest of the graph, the operands of every
    expression, the store a read names. Every one of those is checked here,
    against a tree built with the with-statement.
  - **Reaching the graph from ANOTHER process** (``TestAcrossProcesses``). In the
    process that built it the registry already holds every wrapper, so a
    reconstruction there is a lookup and proves nothing. The graph is built under
    ``with DCG_SHARED`` - the layer allocates through the allocator protocol, so
    it lands in shared memory - and a FORKED child with its registries emptied
    reconstructs it from an address alone, in its own process, and prints the
    same dump the test prints. The two must match line for line.

    (A fresh interpreter cannot be used: the allocator unlinks its shared region
    as soon as it is mapped, so the region is shared only with processes forked
    from the builder. ``bake_reconstruct_probe.py`` says so where the child is
    written.)

Oracle: the tree the test built - its shape, its node types, its edges, its
parent links, and the blocks its operands are - plus, for the cross-process half,
a process that has nothing but an address.
"""

import gc
import itertools
import os
import sys
import unittest

from decision_graph.decision_tree.bake.c_action import (
    ActionNode,
    CancelAction,
    ClearAction,
    LongAction,
    NoAction,
    ShortAction,
)
from decision_graph.decision_tree.bake.c_allocator_protocol import DCG_SHARED
from decision_graph.decision_tree.bake.c_collections import AttrExpression, LogicMapping
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_expr import (
    BinaryExpression,
    ExpressionNode,
    ExpressionOperator,
    TernaryExpression,
    UnaryExpression,
)
from decision_graph.decision_tree.bake.c_hierarchy import BreakpointNode, NodeEvalPathView, RootLogicNode
from decision_graph.decision_tree.bake.c_node import NODE_REGISTRY, LogicNode, PlaceholderNode
from decision_graph.decision_tree.bake.c_reconstruct import (
    c_dcg_node_reconstruct_from_address,
    c_dcg_node_root_from_address,
)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from bake_reconstruct_probe import dump_tree  # noqa: E402  (the probe module, for the dump both sides print)

# The store's name is part of every read's display text, and a long one overflows
# the buffer the C layer composes a repr in. It is also how the manager registers
# a group, so two trees in one process may not share it.
_store_counter = itertools.count()


def build_deep_tree() -> RootLogicNode:
    """The four-level trading decision, built the way a build is written.

    A root, a store inside it, and branches that nest: four levels, three leaf
    types, expressions of two arities, and spare arms the C layer closed rather
    than the build - so a reconstruction has both the nodes the build made and
    the ones it grew to bring back.
    """
    store = LogicMapping(name=f'rs{next(_store_counter)}')
    with RootLogicNode(name='Entry') as root:
        with store:
            flat = BinaryExpression(ExpressionOperator.eq, store['exposure'], ConstantNode(0))
            with flat:
                with store['volatility'] > ConstantNode(0.25):
                    with store['down_prob'] > ConstantNode(0.2):
                        with store['ttl'] > ConstantNode(30):
                            LongAction()
                        ShortAction()
                    with store['up_prob'] < ConstantNode(-0.1):
                        CancelAction()
                with store['exposure'] > ConstantNode(0):
                    ClearAction()
    return root


def forget(node: LogicNode) -> None:
    """Make the layer forget one node's wrapper, without freeing the block.

    A wrapper the build made owns its block, so the node object has to stay alive
    for the address to keep meaning anything - which means dropping the registry
    entry and nothing else.
    """
    NODE_REGISTRY.pop(node.address, None)


def forget_graph(node: LogicNode) -> None:
    """Make the layer forget a whole graph: every node under it and above it."""
    stack, seen = [node], set()
    while stack:
        current = stack.pop()
        if current.address in seen:
            continue
        seen.add(current.address)
        forget(current)
        stack.extend(current.children.values())
        if current.parent is not None:
            stack.append(current.parent)


# The class every node type has to come back as. The read and the call are the
# two that need a word: a read names its store, which is state and not a type
# (AttrExpression is what one is; a bare variable node the other), and a call has
# no class to come back as at all - its callee is composed into the node's display
# text and stored nowhere - so the generic view is the honest answer for it.
CLASSES_BY_TYPE = {
    'INPUT': ConstantNode, 'TRUE': ConstantNode, 'FALSE': ConstantNode,
    'DOUBLE': ConstantNode, 'STRING': ConstantNode, 'INT': ConstantNode,
    'OP': ExpressionNode, 'UNARY': UnaryExpression, 'BINARY': BinaryExpression,
    'TERNARY': TernaryExpression, 'CALL': LogicNode, 'SPECIAL': LogicNode,
    'ACTION': ActionNode, 'NOACTION': NoAction, 'LONGACTION': LongAction,
    'SHORTACTION': ShortAction, 'CANCELACTION': CancelAction,
    'CLEARACTION': ClearAction, 'PLACEHOLDER': PlaceholderNode,
    'ROOT': RootLogicNode, 'BREAKPOINT': BreakpointNode,
}


def assert_classes(test, node: LogicNode) -> None:
    """Every node of a rebuilt graph, as the class its type names."""
    for current in walk(node):
        expected = CLASSES_BY_TYPE[current.type]
        with test.subTest(type=current.type):
            test.assertIs(type(current), expected)


def walk(node: LogicNode) -> list:
    """Every node under one, in no particular order."""
    found, stack = [], [node]
    while stack:
        current = stack.pop()
        found.append(current)
        stack.extend(current.children.values())
    return found


class TestRestoration(unittest.TestCase):
    """Contract: a reconstruction restores the graph, not just the block.

    Expected behavior:
        - a node the layer holds comes back as the instance the build made;
        - a graph the layer no longer holds comes back whole: the same shape, the
          same types, the same edges, the same parent links, node for node;
        - a sub-node is a way in: everything above it comes back too;
        - an expression holds a node for every one of its arguments;
        - a node the C layer grew is reachable like any other.
    """

    def test_00_a_held_node_comes_back_as_itself(self) -> None:
        """The registry is the identity: one block, one wrapper."""
        node = ConstantNode(7)
        self.assertIs(c_dcg_node_reconstruct_from_address(node.address), node)
        self.assertIs(c_dcg_node_reconstruct_from_address(node.address), node)  # and again

    def test_01_a_whole_graph_comes_back(self) -> None:
        """The built tree and the rebuilt one describe the same graph.

        Every wrapper is dropped from the registry first, so the reconstruction
        has nothing to lean on and has to rebuild the tree from the C blocks. The
        two dumps are compared as text, so a missing child, a lost edge, a wrong
        class or an absent parent shows up as a differing line rather than as a
        passing assertion about one of them.
        """
        root = build_deep_tree()
        built = dump_tree(root)
        forget_graph(root)

        rebuilt = c_dcg_node_reconstruct_from_address(root.address)
        self.assertIs(type(rebuilt), RootLogicNode)
        self.assertEqual(dump_tree(rebuilt), built)
        self.assertGreater(len(built), 10)
        assert_classes(self, rebuilt)

    def test_02_a_sub_node_reaches_the_root(self) -> None:
        """A node in the middle of a graph is a way in, not a dead end.

        The walk goes up as well as down, so a leaf's address leads to the whole
        tree - and the root found from it describes the same graph the build made.
        """
        root = build_deep_tree()
        built = dump_tree(root)

        leaves = [node for node in walk(root) if isinstance(node, (LongAction, ShortAction, CancelAction, ClearAction))]
        self.assertGreaterEqual(len(leaves), 4)
        leaf = leaves[0]

        forget_graph(root)
        rebuilt_leaf = c_dcg_node_reconstruct_from_address(leaf.address)
        self.assertIs(type(rebuilt_leaf), type(leaf))

        rebuilt_root = c_dcg_node_root_from_address(leaf.address)
        self.assertIs(type(rebuilt_root), RootLogicNode)
        self.assertEqual(dump_tree(rebuilt_root), built)
        assert_classes(self, rebuilt_root)

    def test_03_an_expression_holds_its_operands(self) -> None:
        """Every argument of a rebuilt expression reads from a node again.

        Two kinds, and both are restored: an argument that REFERS to a node - the
        operand comes back as a wrapper over the same block - and one whose slot a
        value was folded into, where a literal carrying that value stands in
        because the operand node itself is gone by design. That is what folding
        is for.
        """
        left, right = ExpressionNode(), ExpressionNode()
        expression = left + right

        forget_graph(expression)
        rebuilt = c_dcg_node_reconstruct_from_address(expression.address)

        self.assertIs(type(rebuilt), BinaryExpression)
        self.assertEqual(rebuilt.n_args, 2)
        operands = rebuilt.operands
        self.assertEqual([type(operand) for operand in operands], [ExpressionNode, ExpressionNode])
        self.assertEqual([operand.address for operand in operands], [left.address, right.address])

        folded = ConstantNode(1) + ConstantNode(2)
        forget_graph(folded)
        rebuilt_folded = c_dcg_node_reconstruct_from_address(folded.address)
        self.assertEqual([operand.value for operand in rebuilt_folded.operands], [1, 2])

    def test_04_a_read_comes_back_with_its_store(self) -> None:
        """A read is a view of a store: both come back, and the read still reads.

        The group is the field no node type can give - a read IS a variable node
        in C, and the class it comes back as is decided by the store it names.
        """
        store = LogicMapping(name='rb')
        with store:
            read = store['x']
        store['x'] = 4.25

        forget_graph(read)
        rebuilt = c_dcg_node_reconstruct_from_address(read.address)

        self.assertIs(type(rebuilt), AttrExpression)
        self.assertIs(rebuilt.logic_group, store)
        self.assertEqual(rebuilt.value, 4.25)

    def test_05_a_node_the_c_layer_grew_is_reachable(self) -> None:
        """A node Python never made: an arm the build left, closed by the C layer.

        The arm is found by its C TYPE, not by its Python class, and that is not a
        shortcut: the node was converted in place from a placeholder, and the
        wrapper the layer still holds for it is the class it WAS - the layer has
        no event that refreshes a wrapper's class yet. A reconstruction has no such
        history to inherit: it reads the type and gives the class that names it.
        """
        with RootLogicNode(name='rg') as root:
            with ConstantNode(True):
                LongAction()  # the TRUE arm; the FALSE arm is left to the C layer

        branch = next(iter(root.children.values()))
        spare = next(child for child in branch.children.values() if child.type == 'NOACTION')

        # The layer is made to forget the wrapper it holds for the arm - the one
        # the build made for the placeholder it was, which the C layer then
        # converted in place (the layer has no event that refreshes a wrapper's
        # class yet). What is under test is what a reconstruction makes of the
        # node, and it has no such history to inherit: it reads the type.
        forget(spare)
        rebuilt = c_dcg_node_reconstruct_from_address(spare.address)
        self.assertIs(type(rebuilt), NoAction)
        self.assertEqual(rebuilt.repr, 'NoAction')

    def test_05b_a_rebuilt_root_carries_its_record(self) -> None:
        """The eval path is a FIELD of a root's wrapper, so a rebuilt root has one.

        And it is a view, not a copy: it is taken over the record the C root
        holds, so what a rebuilt root reports through it is whatever that record
        holds at the moment of the read - including a walk that happened after
        the wrapper was rebuilt.
        """
        root = build_deep_tree()

        forget(root)  # force the miss: the reconstruction is what builds this wrapper
        rebuilt = c_dcg_node_reconstruct_from_address(root.address)

        self.assertIsInstance(rebuilt, RootLogicNode)
        self.assertIsInstance(rebuilt.eval_path, NodeEvalPathView)
        self.assertEqual(len(rebuilt.eval_path), 0)  # no walk yet, and the record says so
        self.assertEqual(rebuilt.eval_path.code_name, 'OK')

        # A walk through the OTHER wrapper fills the same record: the store this
        # tree reads was never filled, so it stops at the first branch - and the
        # record of that is what both wrappers now report.
        with self.assertRaises(RuntimeError):
            root.eval()
        self.assertEqual(len(rebuilt.eval_path), 2)
        self.assertEqual(rebuilt.eval_path.code_name, 'UNBOUND')
        self.assertIsNone(rebuilt.eval_path.leaf)
        self.assertIsNotNone(rebuilt.eval_path.failed)
        self.assertEqual(rebuilt.eval_path.nodes, root.eval_path.nodes)
        print('rebuilt record:', rebuilt.eval_path)

    def test_06_a_null_address_is_refused(self) -> None:
        """There is no node at zero, and wrapping it is not a value."""
        with self.assertRaises(ValueError):
            c_dcg_node_reconstruct_from_address(0)

    def test_07_a_rebuilt_wrapper_owns_nothing(self) -> None:
        """A reconstruction may not release a block the layer did not hand it.

        The default is the non-owning one, so dropping the rebuilt wrapper leaves
        the C node standing - which is what the second reconstruction proves.
        (``node`` is kept alive on purpose: the wrapper a BUILD made owns its
        block, so letting it go frees the node and the address would then name
        freed memory.)
        """
        node = ConstantNode(3)
        forget(node)
        rebuilt = c_dcg_node_reconstruct_from_address(node.address)
        del rebuilt
        gc.collect()

        again = c_dcg_node_reconstruct_from_address(node.address)
        self.assertEqual(again.value, 3)
        self.assertIs(type(again), ConstantNode)


class TestAcrossProcesses(unittest.TestCase):
    """Contract: a graph in shared memory is reachable from a process that did not build it.

    Expected behavior:
        - a child with its registries emptied reconstructs the ROOT from its
          address and prints the same tree the builder prints;
        - it does the same from a LEAF's address, finding the root above it;
        - the dumps come from two processes, so neither can pass by sharing a
          Python object with the other.
    """

    def _dump_in_child(self, body: str) -> list:
        """Fork a child, run `body` in it, and read back what it printed.

        The child is a separate process with its own heap and (when the body says
        so) no registries. It leaves through ``os._exit``: a forked child of a
        process holding C extensions has no business running the parent's
        cleanup, and its exit status is the only thing the parent needs from it.
        """
        read_fd, write_fd = os.pipe()
        pid = os.fork()

        if pid == 0:
            os.close(read_fd)
            # Everything the child says goes into the pipe: its dump on stdout,
            # and its reason on stderr if it dies before printing one. The streams
            # are REBOUND, not just the descriptors: under a test runner they are
            # capture objects of the parent's, and a child's prints would land in
            # the runner's report instead of here.
            out = os.fdopen(write_fd, 'w')
            sys.stdout, sys.stderr = out, out
            try:
                exec(compile(body, '<child>', 'exec'), {'__name__': '__main__', '__file__': __file__})
                out.flush()
                os._exit(0)
            except BaseException:
                import traceback

                traceback.print_exc()
                os._exit(3)

        os.close(write_fd)
        with os.fdopen(read_fd) as stream:
            printed = stream.read().splitlines()
        _, status = os.waitpid(pid, 0)

        self.assertEqual(status, 0, f'the child failed (status {status}):\n' + '\n'.join(printed))
        return printed

    def _probe_body(self, address: int, mode: str) -> str:
        """The child's program: forget everything, reconstruct, print the dump."""
        return (
            f'from bake_reconstruct_probe import clear_registries, dump_tree, main\n'
            f'clear_registries()\n'
            f'sys.argv = ["probe", {hex(address)!r}, {mode!r}]\n'
            f'main()\n'
        )

    def _setup(self) -> str:
        """The child's import path: this test's directory, and the package's."""
        paths = os.pathsep.join(path for path in sys.path if path)
        return f'import sys, os\nsys.path.insert(0, {os.path.dirname(os.path.abspath(__file__))!r})\n'

    def test_00_another_process_reconstructs_the_root(self) -> None:
        """The whole tree, read by a process that never wrapped it."""
        with DCG_SHARED:
            root = build_deep_tree()
            built = dump_tree(root)
            printed = self._dump_in_child(self._setup() + self._probe_body(root.address, 'root'))

        self.assertEqual(printed, built)
        self.assertGreater(len(printed), 10)

    def test_01_another_process_reconstructs_from_a_leaf(self) -> None:
        """A leaf's address, and the whole tree above it, in the other process."""
        with DCG_SHARED:
            root = build_deep_tree()
            built = dump_tree(root)
            leaf = next(node for node in walk(root) if isinstance(node, LongAction))
            printed = self._dump_in_child(self._setup() + self._probe_body(leaf.address, 'root'))

        self.assertEqual(printed, built)

    def test_02_the_child_rebuilds_with_an_empty_registry(self) -> None:
        """The child starts with nothing wrapped and ends with the graph wrapped.

        The oracle is the child's own registry, reported from inside it: empty
        before the reconstruction, holding the rebuilt graph after - which is what
        makes the two dumps above a reconstruction rather than a lookup.
        """
        with DCG_SHARED:
            root = build_deep_tree()

        body = self._setup() + (
            'from decision_graph.decision_tree.bake.c_node import NODE_REGISTRY\n'
            'from bake_reconstruct_probe import clear_registries\n'
            'from decision_graph.decision_tree.bake.c_reconstruct import c_dcg_node_root_from_address\n'
            'clear_registries()\n'
            'print("empty", len(NODE_REGISTRY))\n'
            f'node = c_dcg_node_root_from_address({root.address})\n'
            'print("rebuilt", node.type, len(NODE_REGISTRY))\n'
        )
        printed = self._dump_in_child(body)

        self.assertEqual(printed[0], 'empty 0')
        self.assertTrue(printed[1].startswith('rebuilt ROOT '), printed)
        self.assertGreater(int(printed[1].split()[2]), 1)
