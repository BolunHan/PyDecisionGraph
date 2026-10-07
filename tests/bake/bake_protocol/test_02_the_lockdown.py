"""What a baked graph refuses: the structure is what it was baked with.

The lockdown is the half of a bake that a caller notices, and it is entirely a
matter of refusals - there is nothing to assert about a graph that stopped moving
except that it stopped. So most cases here name a mutation that a graph under
construction accepts, and then assert that the SAME mutation is refused once the
graph has been baked. The first half is what makes the second half mean
something, and where doing it to the case's own tree would spoil the second half,
the case does it to a second tree.

What a bake does NOT stop is a VALUE: a store is sealed in shape, and the case
that closes this file is the one that says so.
"""

import unittest

from decision_graph.decision_tree.bake.c_action import ClearAction, NoAction
from decision_graph.decision_tree.bake.c_edge import ELSE_CONDITION
from decision_graph.decision_tree.exc import BakeFailureError

from bake_case import BakeCase


class TestTheLockdown(BakeCase):
    """The mutations a baked graph refuses."""

    def test_00_a_branch_takes_no_more_children(self) -> None:
        other = self.tree('Other')
        late = NoAction()
        other.outer.append(late, ELSE_CONDITION)  # an unbaked branch takes one ...
        late.detach()  # ... and gives it back

        tree = self.tree()
        tree.root.bake()
        with self.assertRaises(RuntimeError) as caught:
            tree.outer.append(NoAction(), ELSE_CONDITION)
        self.assertIn('-10', str(caught.exception))  # DCG_ERR_BUSY: the node is frozen

    def test_01_a_child_cannot_be_detached_from_a_baked_graph(self) -> None:
        other = self.tree('Other')
        other.flat.detach()  # an unbaked graph lets a branch go
        self.assertFalse(any(child is other.flat for child in other.outer.children.values()))

        tree = self.tree()
        tree.root.bake()
        with self.assertRaises(RuntimeError) as caught:
            tree.flat.detach()
        self.assertIn('-10', str(caught.exception))

    def test_02_a_child_cannot_be_replaced_in_a_baked_graph(self) -> None:
        other = self.tree('Other')
        other.outer.replace(other.flat, ClearAction())  # the same call, unbaked

        tree = self.tree()
        tree.root.bake()
        with self.assertRaises(RuntimeError) as caught:
            tree.outer.replace(tree.flat, ClearAction())
        self.assertIn('-10', str(caught.exception))

    def test_03_a_branch_cannot_be_overwritten_in_a_baked_graph(self) -> None:
        tree = self.tree()
        condition = list(tree.outer.children)[1]  # the arm the clear action hangs by
        tree.root.bake()

        with self.assertRaises(RuntimeError) as caught:
            tree.outer.overwrite(ClearAction(), condition)
        self.assertIn('-10', str(caught.exception))

    def test_04_the_root_keeps_its_one_arm(self) -> None:
        tree = self.tree()
        tree.root.bake()

        with self.assertRaises(RuntimeError):
            tree.root.append(NoAction())

    def test_05_the_lockdown_reaches_the_whole_graph_not_just_the_root(self) -> None:
        tree = self.tree()
        tree.root.bake()

        # Every node of the graph is frozen, so the deepest branch refuses a
        # mutation as readily as the root does.
        with self.assertRaises(RuntimeError):
            tree.inner.append(NoAction(), ELSE_CONDITION)
        with self.assertRaises(RuntimeError):
            tree.long.detach()

    def test_06_an_operand_cannot_be_rebound_under_a_baked_node(self) -> None:
        tree = self.tree()
        tree.outer.bind(1, self.const(7))  # an unbaked node takes another operand ...

        tree.root.bake()
        with self.assertRaises(RuntimeError) as caught:
            tree.outer.bind(1, self.const(9))  # ... a baked one is what it was baked as
        self.assertIn('-10', str(caught.exception))

    def test_07_a_graph_mutated_out_of_shape_is_refused_later(self) -> None:
        tree = self.tree()

        # The same detach the bake would have refused, done BEFORE it: the layer
        # allows it, and the graph is left with one arm where its arity reads two.
        tree.flat.detach()
        self.assertFalse(any(child is tree.flat for child in tree.outer.children.values()))

        # Which is what a bake is for: the shape an evaluation assumes is
        # established there, so the graph cannot reach an evaluation in this
        # state at all.
        with self.assertRaises(BakeFailureError) as caught:
            tree.root.bake()
        self.assertIs(caught.exception.report.node, tree.outer)
        self.assertIn('TYPE', str(caught.exception))

    def test_08_the_store_goes_on_taking_values(self) -> None:
        tree = self.tree()
        tree.root.bake()

        # The shape is sealed; the contents are the whole point of a store. The
        # decision follows the value in, which is what "baked once, fed many
        # times" means.
        self.fill(signal=2.0)
        self.assertIs(tree.root.eval(), tree.long)

        self.fill(signal=-2.0)
        self.assertIs(tree.root.eval(), tree.flat)

        self.fill(signal=0.5)
        self.assertIs(tree.root.eval(), tree.cancel)


if __name__ == '__main__':
    unittest.main()
