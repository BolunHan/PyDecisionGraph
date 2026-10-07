"""A deep graph, walked from its root: the whole decision, level by level.

The graphs here are generated rather than written out, because what is being
measured is depth. Each level is a branch ``signal > i`` whose TRUE arm holds the
level below and whose FALSE arm is a leaf of its own, so a signal decides which
level the walk stops at and which leaf it lands on - the depth of the walk is the
number of levels the signal satisfied.

What this file is really checking is that a walk of many levels is a walk: every
level is evaluated, every level keeps its value, the record holds the levels in
order, and the answer is the leaf reached - with the values of the moment deciding
it, so the same graph decides differently as the store changes.
"""

import unittest

from decision_graph.decision_tree.bake.c_action import (
    CancelAction,
    ClearAction,
    LongAction,
    NoAction,
    ShortAction,
)
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode

from eval_case import EvalCase

# The leaf classes, cycled level by level: a walk's landing level is readable
# from the class of what it landed on.
LEAVES = (LongAction, ShortAction, CancelAction, ClearAction, NoAction)


class TestADeepGraph(EvalCase):
    """``depth`` levels of ``signal > i``, and the leaf each signal reaches."""

    def build(self, depth: int):
        """Build the chain, and answer with the root and the sections it made.

        Each level i takes the true arm for the level below it and the false arm
        for a leaf of its own; the deepest level's true arm is a leaf too. The
        nodes are made in that order on purpose - a branch written inside a scope
        takes the arm the enclosing node has left, true before false.
        """
        made = []

        def level(index: int):
            gate = read > ConstantNode(index)
            with gate:
                if index + 1 < depth:
                    made.append(level(index + 1))
                else:
                    made.append(LEAVES[index % len(LEAVES)]())
                made.append(LEAVES[(index + 1) % len(LEAVES)]())
            return gate

        with RootLogicNode(name='Deep') as root:
            with self.mapping:
                read = self.mapping['signal']
                level(0)
        return root, read, made

    def test_00_a_signal_decides_the_depth_of_the_walk(self) -> None:
        root, read, made = self.build(depth=6)
        cases = (
            (0.5, CancelAction, 4),  # level 0 held, level 1 did not
            (2.5, NoAction, 6),  # levels 0-2 held, level 3 did not
            (99.0, LongAction, 8),  # every level held: the deepest leaf
        )
        for signal, expected, path_length in cases:
            with self.subTest(signal=signal):
                self.fill(signal=signal)
                landed = root.eval()
                self.assertIsInstance(landed, expected)
                self.assertEqual(len(root.eval_path), path_length)
                self.assertIs(root.eval_path.leaf, landed)
                self.assertEqual(root.eval_path.code_name, 'OK')
                self.show(f'signal={signal:<5}', 'landed', type(landed).__name__, 'path', path_length)

    def test_01_every_level_of_the_walk_keeps_its_value(self) -> None:
        root, read, made = self.build(depth=6)
        self.fill(signal=99.0)
        root.eval()

        gates = [node for node in root.eval_path if node.type == 'BINARY']
        self.assertEqual(len(gates), 6)
        for index, gate in enumerate(gates):
            self.assertIs(gate.out.value, True, f'level {index} was satisfied')
            self.assertIs(gate.operands[0], read)

    def test_02_the_same_graph_decides_again_on_new_values(self) -> None:
        root, read, made = self.build(depth=6)
        for signal, expected in ((99.0, LongAction), (0.5, CancelAction), (99.0, LongAction), (1.5, ClearAction)):
            with self.subTest(signal=signal):
                self.fill(signal=signal)
                self.assertIsInstance(root.eval(), expected)

    def test_03_the_walk_is_recorded_level_by_level(self) -> None:
        root, read, made = self.build(depth=4)
        self.fill(signal=5.0)
        landed = root.eval()

        path = root.eval_path
        self.assertEqual(path[0].type, 'ROOT')
        self.assertIs(path[-1], landed)
        self.assertEqual(path.nodes[0], path[0])
        for node in path.nodes[1:-1]:
            self.assertEqual(node.type, 'BINARY')
        self.show('path', ' -> '.join(node.type for node in path.nodes))

    def test_04_a_deep_graph_reads_the_store_at_every_level(self) -> None:
        """One read, six branches, and the value of the moment for all of them."""
        root, read, made = self.build(depth=6)
        self.fill(signal=3.5)
        root.eval()
        self.assertEqual(read.out.value, 3.5)
        self.assertIs(read.out.is_ref, True)
        self.assertEqual(read.out.type_name, 'double_ref')


class TestAWiderGraph(EvalCase):
    """Two entries, several levels, and the arms a build leaves standing."""

    def test_00_a_graph_over_two_entries(self) -> None:
        with RootLogicNode(name='Pair') as root:
            with self.mapping:
                a = self.mapping['a']
                b = self.mapping['b']
                first = a > b
                with first:
                    second = b > ConstantNode(0)
                    with second:
                        LongAction()
                        CancelAction()
                    ClearAction()

        cases = ((5, 1, LongAction), (5, -1, CancelAction), (1, 5, ClearAction))
        for a_value, b_value, expected in cases:
            with self.subTest(a=a_value, b=b_value):
                self.fill(a=a_value, b=b_value)
                self.assertIsInstance(root.eval(), expected)

    def test_01_the_leaf_of_an_arm_the_build_never_filled(self) -> None:
        """A reserved arm is closed by the layer: the walk lands there, and says so.

        A branch whose arm was left standing is closed with an auto no-action when
        the node around it is left, so the graph always has somewhere to land. The
        wrapper the registry still holds for it is the class the node WAS - the
        layer reports the type, and the type is the truth both sides agree on.
        """
        with RootLogicNode(name='Standing') as root:
            with self.mapping:
                read = self.mapping['signal']
                gate = read > ConstantNode(0)
                with gate:
                    LongAction()

        self.fill(signal=-1.0)
        landed = root.eval()
        self.assertEqual(landed.type, 'NOACTION')  # closed, not a hole in the graph
        self.assertTrue(landed.autogen)  # generated by the layer, not by the build
        self.assertEqual([node.type for node in root.eval_path], ['ROOT', 'BINARY', 'NOACTION'])
        self.show('a signal below the gate', 'landed on', landed.type)

    def test_02_every_walk_replaces_the_record(self) -> None:
        with RootLogicNode(name='Recorded') as root:
            with self.mapping:
                read = self.mapping['signal']
                with read > ConstantNode(0):
                    LongAction()
                    ShortAction()

        self.fill(signal=1.0)
        root.eval()
        first_seq = root.eval_path.seq_id
        first_leaf = root.eval_path.leaf

        self.fill(signal=-1.0)
        second = root.eval()
        self.assertNotEqual(root.eval_path.seq_id, first_seq)
        self.assertIs(root.eval_path.leaf, second)
        self.assertIsNot(root.eval_path.leaf, first_leaf)


if __name__ == '__main__':
    unittest.main()
