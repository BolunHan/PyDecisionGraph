"""Wrapper-level tests for the closing of a built graph.

A graph is closed when the `with` block around a node ends: the exit half fills
the arm the build never took and turns whatever is still a stand-in into a real
no-action (see ``tests/bake/test_c_node_hierarchy.c``). What is checked here is
the half a caller sees through the wrappers - which objects the closed gate
holds, what they report, and what happened to the stand-ins the close retired.

Expected behavior:
  - a closed gate holds a real ``NoAction`` on the FALSE arm, whose parent is
    the gate and whose edge is the FALSE edge;
  - a reserved placeholder the close retires is a husk afterwards: the block it
    stood for was released, and the wrapper learns that from the allocator's
    FREE pass, not from a node event;
  - a stand-in the builder left holding a subtree hands that subtree to the
    no-action that replaces it, and the children know their new parent.

ORDER. The parent wrapper's map keeps both arms in build order - TRUE first,
FALSE second - through both the build and the close. A replacement is ONE event
(``DCG_NODE_EVENT_CHILD_UPDATED``): the slot changes occupant and keeps its
place, so the wrapper re-keys the same condition where it stands. Consolidation
is silent about the stand-in's removal for the same reason - it unlinks without
an event and links back at the same anchor. Both the filled-arm case and the
untouched-pair case are pinned below.

Oracle: the C layer's own reported types and the wrappers handed in, never a
re-derivation of the wrapper's bookkeeping.
"""

import unittest

from decision_graph.decision_tree.bake.c_action import LongAction
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_edge import FALSE_CONDITION, NO_CONDITION, TRUE_CONDITION
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode
from decision_graph.decision_tree.bake.c_node import NODE_REGISTRY


def _gate() -> BinaryExpression:
    """A branching node of the shape a build enters and closes."""
    return BinaryExpression(ExpressionOperator.gt, ConstantNode(1.0), ConstantNode(0.0))


def _order(gate: BinaryExpression) -> list:
    """The arms a gate holds, in the order its wrapper keeps them.

    The built-in conditions answer their own edge with ``__bool__`` - the TRUE
    edge is ``True`` and the FALSE edge is ``False`` - so the list reads as the
    order the build put them in.
    """
    return [bool(edge) for edge in gate.children]


def _is_husk(wrapper) -> bool:
    """Whether a node wrapper has been invalidated by its block's release."""
    try:
        wrapper.type
    except RuntimeError:
        return True
    return False


class TestTheClosedGate(unittest.TestCase):
    """Contract: closing a gate leaves real arms, and retires the stand-ins."""

    def test_00_a_reserved_pair_nothing_filled_keeps_its_build_order(self) -> None:
        """An untouched gate closes with both arms consolidated in place.

        Entering reserves TRUE first and FALSE second, and neither is replaced
        while the build runs - so the map keeps that order and the close only
        changes what stands on each arm, not where it stands.
        """
        root = RootLogicNode(name='consolidation-order')
        with root:
            gate = _gate()
            with gate:
                reserved_true = gate.children[TRUE_CONDITION]
                reserved_false = gate.children[FALSE_CONDITION]
                true_address = reserved_true.address
                false_address = reserved_false.address
                self.assertEqual(reserved_true.type, 'PLACEHOLDER')
                self.assertEqual(reserved_false.type, 'PLACEHOLDER')
                self.assertEqual(_order(gate), [True, False])

            self.assertEqual(_order(gate), [True, False])
            self.assertEqual([child.type for child in gate.children.values()], ['NOACTION', 'NOACTION'])

            # The false arm is a real no-action, in the slot it always held.
            false_arm = gate.children[FALSE_CONDITION]
            self.assertEqual(false_arm.type, 'NOACTION')
            self.assertEqual(false_arm.repr, 'NoAction')
            self.assertIs(false_arm.parent, gate)
            self.assertEqual(false_arm.condition_to_parent, FALSE_CONDITION)

            # The stand-ins are gone: their blocks were released by the close,
            # and each wrapper took its registry entry with it.
            self.assertTrue(_is_husk(reserved_true))
            self.assertTrue(_is_husk(reserved_false))
            self.assertNotIn(true_address, NODE_REGISTRY)
            self.assertNotIn(false_address, NODE_REGISTRY)
            self.assertIn(false_arm.address, NODE_REGISTRY)
            self.assertEqual(repr(reserved_false), '<PlaceholderNode (Uninitialized)>')

    def test_01_a_built_true_arm_keeps_its_place_in_the_map(self) -> None:
        """A gate whose TRUE arm was built: the slot changes occupant, not place.

        Filling an arm replaces the stand-in, and a replacement is one event -
        ``CHILD_UPDATED`` - so the wrapper re-keys the same condition where it
        stands. The TRUE arm is first before the build and stays first after
        it; the FALSE arm is still the reserved placeholder when the build
        ends, and consolidation converts it in place for the same reason.
        """
        root = RootLogicNode(name='consolidation-built')
        with root:
            gate = _gate()
            with gate:
                reserved_true = gate.children[TRUE_CONDITION]
                reserved_false = gate.children[FALSE_CONDITION]
                true_address = reserved_true.address
                false_address = reserved_false.address
                self.assertEqual(_order(gate), [True, False])

                long_ = LongAction(repr='go long')
                self.assertEqual(_order(gate), [True, False])

            self.assertEqual(_order(gate), [True, False])

            # The map was keyed when the node JOINED, which is before the
            # action wrapper registered itself: the entry is a wrapper over the
            # same block, not necessarily the very object the caller holds.
            true_arm = gate.children[TRUE_CONDITION]
            self.assertEqual(true_arm.address, long_.address)
            self.assertEqual(true_arm.type, 'LONGACTION')

            false_arm = gate.children[FALSE_CONDITION]
            self.assertEqual(false_arm.type, 'NOACTION')
            self.assertIs(false_arm.parent, gate)
            self.assertEqual(false_arm.condition_to_parent, FALSE_CONDITION)

            # Both stand-ins are gone: the filled arm's was released by the
            # connect (the arm was the active node's to release), the other by
            # the close. Each wrapper took its registry entry with it, and the
            # wrapper that stands there now holds its own.
            self.assertTrue(_is_husk(reserved_true))
            self.assertTrue(_is_husk(reserved_false))
            self.assertNotIn(true_address, NODE_REGISTRY)
            self.assertNotIn(false_address, NODE_REGISTRY)
            self.assertIn(true_arm.address, NODE_REGISTRY)

    def test_02_a_stand_in_holding_a_subtree_hands_it_to_the_no_action(self) -> None:
        """A stand-in the builder built under keeps its subtree across the close.

        The subtree was built UNDER the slot rather than in place of it, so the
        no-action that takes the slot owns it afterwards: the child's parent is
        the no-action wrapper, and the arm itself does not move.
        """
        root = RootLogicNode(name='consolidation-subtree')
        with root:
            gate = _gate()
            with gate:
                holder = gate.children[TRUE_CONDITION]
                self.assertEqual(holder.type, 'PLACEHOLDER')

                child = LongAction(repr='carried', auto_connect=False)
                holder.append(child, NO_CONDITION)
                self.assertIs(child.parent, holder)

            self.assertEqual(_order(gate), [True, False])

            moved = gate.children[TRUE_CONDITION]
            self.assertEqual(moved.type, 'NOACTION')
            self.assertIs(child.parent, moved)
            self.assertEqual(moved.children[child.condition_to_parent], child)

            # The stand-in that held the subtree is gone; the subtree is not.
            self.assertTrue(_is_husk(holder))
            self.assertEqual(child.type, 'LONGACTION')


if __name__ == '__main__':
    unittest.main()
