"""Case 001, arm (a): the tree built and walked with the CAPI - the baseline.

This is the graph the other two arms are compared against, so it is written the
way the product writes one: a mapping seeded with ``data=``, a root entered with
``with``, a branch per level, and actions that connect themselves as they are
built. Nothing here reaches into the bake layer.

The mapping is the point. ``LogicMapping`` answers every read LIVE out of its
dict, so feeding it a new value is what moves the walk - the graph is built once
and asked three times, and the three answers are three different leaves.

Transcript: ``artifacts/001_basic_tree/capi.log``
"""

import unittest

from decision_graph.decision_tree.capi import c_abc, c_collection, c_node

from parity import ENTRY, FEED, ROOT, STORE, Recorder, arm_names, check_expected, feed_order, shape_of

GROUP = '001_basic_tree'
CASE = 'capi'


class TestBasicTreeCapi(unittest.TestCase):
    """The baseline: one store, one branch per level, three walks."""

    def test_000_the_tree_decides_what_the_design_says(self) -> None:
        """Feed the store, walk the tree, and record what each value selected."""
        recorder = Recorder(CASE, GROUP)
        store, root_name = arm_names(CASE, STORE, ROOT)
        self.addCleanup(recorder.close)

        mapping = c_collection.LogicMapping(name=store, data={ENTRY: FEED[0]})
        mapping.__enter__()
        self.addCleanup(mapping.__exit__, None, None, None)

        with c_node.RootLogicNode(name=root_name) as root:
            with c_abc.LogicNode(expression=mapping[ENTRY] > 0) as outer:
                with c_abc.LogicNode(expression=mapping[ENTRY] > 1) as inner:
                    c_abc.LongAction()
                    c_abc.CancelAction()
                c_abc.ClearAction()

        recorder.emit('arm.root', 'capi')       # arm-local: not compared
        recorder.emit('arm.store', store)
        recorder.emit('graph.entry', ENTRY)

        for round_index, value in feed_order():
            with self.subTest(**{ENTRY: value}):
                mapping.data[ENTRY] = value

                decision = root()

                leaf_class = type(decision).__name__
                signal = int(decision)
                path = list(root.eval_path)

                check_expected(value, leaf_class, signal)

                recorder.emit(f'{round_index}.feed', value)
                recorder.emit(f'{round_index}.leaf.class', leaf_class)
                recorder.emit(f'{round_index}.leaf.signal', signal)
                recorder.emit(f'{round_index}.path.length', len(path))
                recorder.emit(f'{round_index}.path.shape', shape_of(path, root))
                recorder.emit(f'local.{round_index}.path.text', '|'.join(type(n).__name__ for n in path))

        # Filled in below, once the loop has run: the arms must have built the
        # same SHAPE, and a tree that is the wrong shape would still walk.
        recorder.emit('graph.outer.children', len(outer.children))
        recorder.emit('graph.inner.children', len(inner.children))


if __name__ == '__main__':
    unittest.main()
