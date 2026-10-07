"""Case 001, arm (b): the same tree built with bake, walked from Python.

The graph is the capi's, written in this layer's idiom: a store the build runs
inside, a read of one of its entries, and an operator node per level whose
operands are that read and a literal. The branch is a NODE here rather than a
Python expression evaluated later - which is the difference between the two
layers, and the reason this arm exists.

Two things this arm has to get right, and both are properties of the bake store
rather than of the graph:

  - a read RESERVES its entry. ``mapping[ENTRY]`` does not look a value up; it
    creates the entry if it is new and hands back the node that reads it. The
    value arrives afterwards, and the graph is written first - which is the
    reservation protocol the layer is built around.
  - the store's SHAPE is sealed by the bake and its CONTENTS are not, so the
    same baked graph is fed three times and decides three ways.

Transcript: ``artifacts/001_basic_tree/bake.log``
"""

import unittest

from decision_graph.decision_tree.bake.c_action import CancelAction, ClearAction, LongAction
from decision_graph.decision_tree.bake.c_collections import LogicMapping
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode

from parity import ENTRY, FEED, ROOT, STORE, Recorder, arm_names, check_expected, feed_order, shape_of

GROUP = '001_basic_tree'
CASE = 'bake'


class TestBasicTreeBake(unittest.TestCase):
    """The bake arm: the same graph, walked through the layer's own door."""

    def test_000_the_tree_decides_what_the_design_says(self) -> None:
        """Bake the tree once, feed the store three times, record each walk."""
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

        recorder.emit('arm.root', 'bake')
        recorder.emit('arm.bake.code', report.code_name)
        recorder.emit('arm.bake.locked', report.locked)
        recorder.emit('arm.bake.sealed', report.sealed)
        recorder.emit('arm.store', store)
        recorder.emit('graph.entry', ENTRY)

        for round_index, value in feed_order():
            with self.subTest(**{ENTRY: value}):
                mapping[ENTRY] = value

                decision = root()

                leaf_class = type(decision).__name__
                signal_value = int(decision)
                path = list(root.eval_path)

                check_expected(value, leaf_class, signal_value)

                recorder.emit(f'{round_index}.feed', value)
                recorder.emit(f'{round_index}.leaf.class', leaf_class)
                recorder.emit(f'{round_index}.leaf.signal', signal_value)
                recorder.emit(f'{round_index}.path.length', len(path))
                recorder.emit(f'{round_index}.path.shape', shape_of(path, root))
                recorder.emit(f'local.{round_index}.path.text', '|'.join(type(n).__name__ for n in path))

        recorder.emit('graph.outer.children', len(outer.children))
        recorder.emit('graph.inner.children', len(inner.children))


if __name__ == '__main__':
    unittest.main()
