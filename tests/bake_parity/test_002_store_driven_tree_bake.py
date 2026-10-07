"""Case 002, arm (b): the store-driven tree on bake, walked from Python.

The same graph as arm (a), written in this layer's idiom - and here the compound
gate is a NODE rather than a Python expression evaluated later. ``(level > 2) &
armed`` is one operator node whose operands are a comparison and a read, so the
store is consulted by the walk itself.

Every entry is reserved by a read at build time and filled afterwards, which is
the reservation protocol: the graph is written against entries that do not exist
yet, and the values arrive between walks. All three entries survive the bake -
it seals the store's SHAPE, not its contents - so the same baked node decides
five ways.

Transcript: ``artifacts/002_store_driven_tree/bake.log``
"""

import unittest

from decision_graph.decision_tree.bake.c_action import CancelAction, LongAction, ShortAction
from decision_graph.decision_tree.bake.c_collections import LogicMapping
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode

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
CASE = 'bake'


class TestStoreDrivenTreeBake(unittest.TestCase):
    """The bake arm: a compound gate over three entries, five walks."""

    def test_000_the_tree_decides_what_the_design_says(self) -> None:
        """Bake once, feed three entries per round, record what each round chose."""
        recorder = Recorder(CASE, GROUP)
        store, root_name = arm_names(CASE, STORE_2, ROOT_2)
        self.addCleanup(recorder.close)

        mapping = LogicMapping(name=store)

        with RootLogicNode(name=root_name) as root:
            with mapping:
                level = mapping[LEVEL]
                ret = mapping[RET]
                armed = mapping[ARMED]

                # The gate: a comparison AND a read, as one operator node. The
                # read is an OPERAND here, not a child - it is not a branch the
                # walk descends into, it is a value the gate takes.
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

        recorder.emit('arm.root', 'bake')
        recorder.emit('arm.bake.code', report.code_name)
        recorder.emit('arm.bake.locked', report.locked)
        recorder.emit('arm.bake.sealed', report.sealed)
        recorder.emit('arm.store', store)
        recorder.emit('graph.entries', f'{LEVEL},{RET},{ARMED}')

        for round_index, (level_value, ret_value, armed_value) in feed_order_2():
            with self.subTest(level=level_value, ret=ret_value, armed=armed_value):
                mapping[LEVEL] = level_value
                mapping[RET] = ret_value
                mapping[ARMED] = armed_value

                decision = root()

                leaf_class = type(decision).__name__
                signal_value = int(decision)
                path = list(root.eval_path)

                check_expected_2((level_value, ret_value, armed_value), leaf_class, signal_value)

                recorder.emit(f'{round_index}.feed', f'{level_value},{ret_value},{armed_value}')
                recorder.emit(f'{round_index}.leaf.class', leaf_class)
                recorder.emit(f'{round_index}.leaf.signal', signal_value)
                recorder.emit(f'{round_index}.path.length', len(path))
                recorder.emit(f'{round_index}.path.shape', shape_of(path, root))
                recorder.emit(f'local.{round_index}.path.text',
                              '|'.join(type(n).__name__ for n in path))

        recorder.emit('graph.gate.children', len(gate.children))
        recorder.emit('graph.inner.children', len(inner.children))


if __name__ == '__main__':
    unittest.main()
