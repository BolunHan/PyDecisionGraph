"""Case 002, arm (c): the store-driven tree baked, then walked in C.

Arm (b)'s graph and arm (b)'s bake; the door is the only difference. Every walk
goes through ``GraphEvaluator``, which runs ``c_dcg_root_node_eval`` and reads
the answer off the root's own record - so a compound gate over three entries is
evaluated with no Python frame between the root and the decision.

That is the arm worth having on THIS case in particular. The gate reads two
entries of different tags and the leaf below it reads a third, so a C-side walk
that agreed only by accident would have to agree on three live reads at once.

Transcript: ``artifacts/002_store_driven_tree/bake_cabi.log``
"""

import unittest

from decision_graph.decision_tree.bake.c_action import CancelAction, LongAction, ShortAction
from decision_graph.decision_tree.bake.c_collections import LogicMapping
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode
from tests.bake_c_evaluator import GraphEvaluator

from parity import (
    ARMED,
    LEVEL,
    RET,
    ROOT_2,
    STORE_2,
    Recorder,
    arm_names,
    check_expected_2,
    feed_order_2,
    shape_of,
)

GROUP = '002_store_driven_tree'
CASE = 'bake_cabi'


class TestStoreDrivenTreeWalkedInC(unittest.TestCase):
    """The C arm: the same baked gate, walked without Python in the middle."""

    def test_000_the_tree_decides_what_the_design_says(self) -> None:
        """Bake once, feed three entries per round, walk each through the C door."""
        recorder = Recorder(CASE, GROUP)
        store, root_name = arm_names(CASE, STORE_2, ROOT_2)
        self.addCleanup(recorder.close)

        mapping = LogicMapping(name=store)

        with RootLogicNode(name=root_name) as root:
            with mapping:
                level = mapping[LEVEL]
                ret = mapping[RET]
                armed = mapping[ARMED]

                gate = BinaryExpression(
                    ExpressionOperator.and_,
                    BinaryExpression(ExpressionOperator.gt, level, ConstantNode(2)),
                    armed,
                )
                inner = BinaryExpression(ExpressionOperator.gt, ret, ConstantNode(0.5))

                with gate:
                    with inner:
                        LongAction()
                        CancelAction()
                    ShortAction()

        report = root.bake()
        evaluator = GraphEvaluator()

        recorder.emit('arm.root', 'bake_cabi')
        recorder.emit('arm.bake.code', report.code_name)
        recorder.emit('arm.bake.locked', report.locked)
        recorder.emit('arm.bake.sealed', report.sealed)
        recorder.emit('arm.evaluator', type(evaluator).__module__)
        recorder.emit('arm.store', store)
        recorder.emit('graph.entries', f'{LEVEL},{RET},{ARMED}')

        for round_index, (level_value, ret_value, armed_value) in feed_order_2():
            with self.subTest(level=level_value, ret=ret_value, armed=armed_value):
                mapping[LEVEL] = level_value
                mapping[RET] = ret_value
                mapping[ARMED] = armed_value

                outcome = evaluator.evaluate(root)

                self.assertEqual(outcome.code_name, 'OK', f'the C walk failed: {outcome!r}')
                self.assertIsNotNone(outcome.leaf, 'the C walk reached no leaf')

                leaf_class = type(outcome.leaf).__name__
                signal_value = int(outcome.leaf)
                path = list(root.eval_path)

                check_expected_2((level_value, ret_value, armed_value), leaf_class, signal_value)
                self.assertEqual(outcome.n_nodes, len(path),
                                 'the C record and the walk it wrote disagree on the count')

                recorder.emit(f'{round_index}.feed', f'{level_value},{ret_value},{armed_value}')
                recorder.emit(f'{round_index}.leaf.class', leaf_class)
                recorder.emit(f'{round_index}.leaf.signal', signal_value)
                recorder.emit(f'{round_index}.path.length', len(path))
                recorder.emit(f'{round_index}.path.shape', shape_of(path, root))
                recorder.emit(f'local.{round_index}.path.text',
                              '|'.join(type(n).__name__ for n in path))

        recorder.emit('arm.outcome.code', outcome.code_name)
        recorder.emit('graph.gate.children', len(gate.children))
        recorder.emit('graph.inner.children', len(inner.children))


if __name__ == '__main__':
    unittest.main()
