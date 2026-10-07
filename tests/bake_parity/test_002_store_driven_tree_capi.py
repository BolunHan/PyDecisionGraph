"""Case 002, arm (a): the store-driven tree on the CAPI - the baseline.

Where case 001 fed one entry down one comparison, this one feeds THREE entries
of three different tags and lets them decide together: the gate is a comparison
AND an entry, and the leaf below it is chosen by a third entry. A walk here
cannot be right unless every read it names was answered from the store at the
moment of the walk.

The mapping is seeded with ``data=`` and then written to between walks, which is
what "the store drives the walk" means on this side.

Transcript: ``artifacts/002_store_driven_tree/capi.log``
"""

import unittest

from decision_graph.decision_tree.capi import c_abc, c_collection, c_node

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
CASE = 'capi'


class TestStoreDrivenTreeCapi(unittest.TestCase):
    """Three entries, one gate built from two of them, five walks."""

    def test_000_the_tree_decides_what_the_design_says(self) -> None:
        """Feed three entries per round, walk, and record what each round chose."""
        recorder = Recorder(CASE, GROUP)
        store, root_name = arm_names(CASE, STORE_2, ROOT_2)
        self.addCleanup(recorder.close)

        mapping = c_collection.LogicMapping(name=store, data={LEVEL: 0, RET: 0.0, ARMED: False})
        mapping.__enter__()
        self.addCleanup(mapping.__exit__, None, None, None)

        with c_node.RootLogicNode(name=root_name) as root:
            with c_abc.LogicNode(expression=(mapping[LEVEL] > 2) & mapping[ARMED]) as gate:
                with c_abc.LogicNode(expression=mapping[RET] > 0.5) as inner:
                    c_abc.LongAction()
                    c_abc.CancelAction()
                c_abc.ShortAction()

        recorder.emit('arm.root', 'capi')
        recorder.emit('arm.store', store)
        recorder.emit('graph.entries', f'{LEVEL},{RET},{ARMED}')

        for round_index, (level, ret, armed) in feed_order_2():
            with self.subTest(level=level, ret=ret, armed=armed):
                mapping.data[LEVEL] = level
                mapping.data[RET] = ret
                mapping.data[ARMED] = armed

                decision = root()

                leaf_class = type(decision).__name__
                signal = int(decision)
                path = list(root.eval_path)

                check_expected_2((level, ret, armed), leaf_class, signal)

                recorder.emit(f'{round_index}.feed', f'{level},{ret},{armed}')
                recorder.emit(f'{round_index}.leaf.class', leaf_class)
                recorder.emit(f'{round_index}.leaf.signal', signal)
                recorder.emit(f'{round_index}.path.length', len(path))
                recorder.emit(f'{round_index}.path.shape', shape_of(path, root))
                recorder.emit(f'local.{round_index}.path.text',
                              '|'.join(type(n).__name__ for n in path))

        recorder.emit('graph.gate.children', len(gate.children))
        recorder.emit('graph.inner.children', len(inner.children))


if __name__ == '__main__':
    unittest.main()
