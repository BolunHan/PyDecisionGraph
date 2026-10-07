"""Case 001, arm (c): the same baked tree, walked entirely in C.

The graph is arm (b)'s, built the same way and put through the same ``bake()``.
What differs is the door: nothing here calls ``root()`` or ``root.eval()``.
``GraphEvaluator.evaluate`` takes the root's address, runs
``c_dcg_root_node_eval`` and reads the answer off the root's own record - so
between the root and the decision there is no Python frame at all.

This is the arm a host embedding the layer would run, and it is the one that has
to agree with the Python-driven walk. If (b) and (c) ever disagree, the graph is
not the suspect: both arms walked the same graph, so what differs is the seam
between the two doors.

Transcript: ``artifacts/001_basic_tree/bake_cabi.log``
"""

import unittest

from decision_graph.decision_tree.bake.c_action import CancelAction, ClearAction, LongAction
from decision_graph.decision_tree.bake.c_collections import LogicMapping
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode
from tests.bake_c_evaluator import GraphEvaluator

from parity import ENTRY, FEED, ROOT, STORE, Recorder, arm_names, check_expected, feed_order, shape_of

GROUP = '001_basic_tree'
CASE = 'bake_cabi'


class TestBasicTreeBakeWalkedInC(unittest.TestCase):
    """The C arm: the same baked graph, walked without Python in the middle."""

    def test_000_the_tree_decides_what_the_design_says(self) -> None:
        """Bake once, feed three times, and walk each one through the C door."""
        recorder = Recorder(CASE, GROUP)
        store, root_name = arm_names(CASE, STORE, ROOT)
        self.addCleanup(recorder.close)

        mapping = LogicMapping(name=store)

        with RootLogicNode(name=root_name) as root:
            with mapping:
                signal = mapping[ENTRY]
                outer = BinaryExpression(ExpressionOperator.gt, signal, ConstantNode(0))
                inner = BinaryExpression(ExpressionOperator.gt, signal, ConstantNode(1))
                with outer:
                    with inner:
                        LongAction()
                        CancelAction()
                    ClearAction()

        report = root.bake()
        evaluator = GraphEvaluator()

        recorder.emit('arm.root', 'bake_cabi')
        recorder.emit('arm.bake.code', report.code_name)
        recorder.emit('arm.bake.locked', report.locked)
        recorder.emit('arm.bake.sealed', report.sealed)
        recorder.emit('arm.evaluator', type(evaluator).__module__)
        recorder.emit('arm.store', store)
        recorder.emit('graph.entry', ENTRY)

        for round_index, value in feed_order():
            with self.subTest(**{ENTRY: value}):
                mapping[ENTRY] = value

                outcome = evaluator.evaluate(root)

                self.assertEqual(outcome.code_name, 'OK', f'the C walk failed: {outcome!r}')
                self.assertIsNotNone(outcome.leaf, 'the C walk reached no leaf')

                leaf_class = type(outcome.leaf).__name__
                signal_value = int(outcome.leaf)
                path = list(root.eval_path)

                check_expected(value, leaf_class, signal_value)
                self.assertEqual(outcome.n_nodes, len(path),
                                 'the C record and the walk it wrote disagree on the count')

                recorder.emit(f'{round_index}.feed', value)
                recorder.emit(f'{round_index}.leaf.class', leaf_class)
                recorder.emit(f'{round_index}.leaf.signal', signal_value)
                recorder.emit(f'{round_index}.path.length', len(path))
                recorder.emit(f'{round_index}.path.shape', shape_of(path, root))
                recorder.emit(f'local.{round_index}.path.text', '|'.join(type(n).__name__ for n in path))

        recorder.emit('arm.outcome.code', outcome.code_name)
        recorder.emit('graph.outer.children', len(outer.children))
        recorder.emit('graph.inner.children', len(inner.children))


if __name__ == '__main__':
    unittest.main()
