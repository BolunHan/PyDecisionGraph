"""The door: one call on the root, and what comes back.

A bake is asked of the ROOT and of nothing else, because a graph is entered from
one place - and the answer is a report rather than a code, because what a bake
did is as much a part of it as whether it succeeded. These cases are the entry
itself:

  - what a healthy graph's report says, field by field;
  - that a bake asked for nothing in particular is the whole of a bake;
  - that asking twice is asking once: the second pass locks nothing, seals
    nothing and prepares nothing;
  - that the report is a SNAPSHOT - a second bake does not rewrite the first
    report - and that the graph it reports on is the graph that was handed in.
"""

import unittest

from decision_graph.decision_tree.bake.c_bake import BakeReport

from bake_case import BakeCase


class TestTheDoor(BakeCase):
    """What ``bake`` answers with."""

    def test_00_a_healthy_graph_bakes_and_says_what_it_did(self) -> None:
        tree = self.tree()

        report = tree.root.bake()
        self.show('report', report)

        self.assertIsInstance(report, BakeReport)
        self.assertEqual(report.code, 0)
        self.assertEqual(report.code_name, 'OK')
        self.assertIsNone(report.node)  # nothing refused, so no node to name
        self.assertEqual(report.errors, 0)

        # The walk reached the tree AND the operands that are not in it: the
        # root, the two branches, three actions, and the two literals and the
        # read the comparisons were built over.
        self.assertEqual(report.nodes, 9)
        self.assertEqual(report.depth, 3)

        # Everything it walked is locked, and the store behind the read is sealed.
        self.assertEqual(report.locked, report.nodes)
        self.assertEqual(report.sealed, 1)

        # The record a walk fills was given its room: one entry per level, and
        # the root is one of them.
        self.assertEqual(report.capacity, tree.root.eval_path.capacity)
        self.assertEqual(report.capacity, 4)

    def test_01_a_bake_asked_for_nothing_is_the_whole_of_a_bake(self) -> None:
        tree = self.tree()

        report = tree.root.bake()  # no arguments at all
        self.assertGreater(report.locked, 0)
        self.assertTrue(self.mapping.frozen)

        # ...and saying so explicitly is the same bake.
        other = self.tree('Other')
        explicit = other.root.bake(validate_only=False)
        self.assertGreater(explicit.locked, 0)
        self.assertTrue(other.mapping.frozen)

    def test_02_a_reported_graph_is_the_graph_that_was_handed_in(self) -> None:
        tree = self.tree()
        before = (tree.root.eval_path.capacity, len(tree.root.children), len(tree.outer.children))

        report = tree.root.bake()
        after = (tree.root.eval_path.capacity, len(tree.root.children), len(tree.outer.children))

        self.assertNotEqual(before[0], after[0])  # the record gained its room
        self.assertEqual(before[1:], after[1:])  # the graph's shape did not move
        self.assertEqual(report.nodes, 9)

    def test_03_a_second_bake_locks_nothing(self) -> None:
        tree = self.tree()

        first = tree.root.bake()
        second = tree.root.bake()

        self.assertGreater(first.locked, 0)
        self.assertGreater(first.sealed, 0)
        self.assertEqual(second.locked, 0)
        self.assertEqual(second.sealed, 0)
        self.assertEqual(second.code_name, 'OK')
        self.assertEqual(second.nodes, first.nodes)  # it still walked the graph
        self.assertEqual(second.capacity, first.capacity)

    def test_04_each_bake_answers_with_a_report_of_its_own(self) -> None:
        tree = self.tree()

        first = tree.root.bake()
        second = tree.root.bake()

        # One block each, so what the first pass did is not overwritten by what
        # the second one did.
        self.assertIsNot(first, second)
        self.assertNotEqual(first.address, second.address)
        self.assertEqual(first.locked, 9)
        self.assertEqual(second.locked, 0)

    def test_05_a_report_can_be_made_before_it_is_filled(self) -> None:
        report = BakeReport()

        # A report of its own says what a bake that never ran says, and owns the
        # block it says it in.
        self.assertNotEqual(report.address, 0)
        self.assertEqual(report.code_name, 'OK')
        self.assertIsNone(report.node)
        self.assertEqual((report.errors, report.nodes, report.depth), (0, 0, 0))
        self.assertEqual((report.locked, report.sealed, report.capacity), (0, 0, 0))

        self.assertNotEqual(report.address, BakeReport().address)  # one block each

    def test_06_a_wrapper_that_was_never_built_says_so(self) -> None:
        unbuilt = BakeReport.__new__(BakeReport)  # __cinit__, and no block behind it

        # Every read refuses rather than answering off a NULL, and going away is
        # not a release of anything.
        self.assertIn('Uninitialized', repr(unbuilt))
        for what in ('address', 'code', 'code_name', 'node', 'errors', 'nodes', 'depth', 'locked', 'sealed', 'capacity'):
            with self.assertRaises(RuntimeError):
                getattr(unbuilt, what)

    def test_07_a_root_is_required(self) -> None:
        tree = self.tree()
        report = tree.root.bake()

        # Every other node has its own eval, but a bake is the graph's - asked of
        # a branch it is not a bake at all.
        self.assertFalse(hasattr(tree.outer, 'bake'))
        self.assertIsNone(report.node)


if __name__ == '__main__':
    unittest.main()
