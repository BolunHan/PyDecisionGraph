"""Wrapper tests for ``decision_graph.decision_tree.bake.c_hierarchy``.

Scope is the Python surface only. The graph's own types in C are settled by
``tests/bake/test_c_node_hierarchy.c`` and ``tests/bake/test_c_logic_group.c``;
what is checked here is the wrapper's half of the bargain:

  - a repr is a constructor argument, so the C node is named once at
    construction rather than built unnamed and patched afterwards;
  - a breakpoint's group is held by the wrapper, because the pointer the C node
    carries is borrowed and the group must not outlive it;
  - nothing given means the C layer's own default stands, not a wrapper-side
    substitute.

Only the two types this module owns are reachable here. The action family is
another layer and is not in the build yet.

Oracle: the node's own reported display text, read back from the C node, and the
objects handed in - never a re-derivation of the wrapper's own arithmetic.
"""

import unittest

from decision_graph.decision_tree.bake.c_hierarchy import BreakpointNode, RootLogicNode
from decision_graph.decision_tree.bake.c_logic_group import LogicGroup


class TestRootLogicNode(unittest.TestCase):
    """Contract: the entry point is named at construction.

    Expected behavior:
        - the name given is kept on the wrapper and is the node's display text;
        - with nothing given, the type's own default stands.

    There is one naming field, not two: a root's display text IS its name, so a
    separate repr would only be an alias of it. The name reaches the C node
    through the constructor, so a root is never briefly nameless.
    """

    def test_00_the_default_name_is_the_c_layers_own(self) -> None:
        """Nothing given: the type's default stands, in both places."""
        root = RootLogicNode()
        self.assertEqual(root.name, 'Entry Point')
        self.assertEqual(root.repr, 'Entry Point')

    def test_01_a_name_is_kept_and_is_the_display_text(self) -> None:
        """The name reaches both the wrapper and the C node."""
        root = RootLogicNode(name='Plan')
        self.assertEqual(root.name, 'Plan')
        self.assertEqual(root.repr, 'Plan')

    def test_02_a_root_is_its_own_root(self) -> None:
        """The entry point has no parent to belong to."""
        self.assertIsNone(RootLogicNode().parent)


class TestBreakpointNode(unittest.TestCase):
    """Contract: a breakpoint names the group it breaks out of.

    Expected behavior:
        - a group handed in is the one reported back;
        - with no group, it names none;
        - its display text follows the same default-or-given rule as a root's.

    ``break_from`` is a borrowed pointer in C - the group outlives every
    breakpoint raised from it - so the wrapper holding the Python object is what
    keeps the pointer valid.
    """

    def test_00_a_fresh_breakpoint_has_the_default_text(self) -> None:
        """Nothing given: the type's default stands."""
        self.assertEqual(BreakpointNode().repr, 'Breakpoint')

    def test_01_a_repr_becomes_the_display_text(self) -> None:
        """An explicit repr is the display text."""
        self.assertEqual(BreakpointNode(repr='stop here').repr, 'stop here')

    def test_02_a_fresh_breakpoint_names_no_group(self) -> None:
        """Nothing to break out of: no group is named."""
        self.assertIsNone(BreakpointNode().break_from)

    def test_03_the_group_is_the_one_handed_in(self) -> None:
        """The group reaches the C node, and the wrapper reports it back."""
        group = LogicGroup(name='scope')
        breakpoint = BreakpointNode(break_from=group)
        self.assertIs(breakpoint.break_from, group)

    def test_04_two_breakpoints_name_their_own_groups(self) -> None:
        """The group belongs to the breakpoint, not to the class."""
        inner = LogicGroup(name='inner')
        outer = LogicGroup(name='outer')
        self.assertIs(BreakpointNode(break_from=inner).break_from, inner)
        self.assertIs(BreakpointNode(break_from=outer).break_from, outer)


class TestTheBreakDoor(unittest.TestCase):
    """Contract: a break is a node the caller keeps, and it names where it left.

    Expected behavior:
        - ``break_`` hands back the breakpoint that took the arm, and that
          wrapper IS the block in the graph - not a second handle on a node of
          the same kind;
        - the breakpoint reports itself as waiting until something resumes into
          it, and ``linked_to`` is None until then;
        - ``connect`` is what resumes into it, and the child it takes comes back
          through ``linked_to`` and reports the breakpoint as its parent;
        - ``get_breakpoint`` on the root finds that same waiting breakpoint,
          which is what lets one function build up to a break and another build
          on from it.

    This is the door that makes a graph assemblable in pieces. What it does NOT
    yet do is the reference idiom's other half - ``with root.get_breakpoint():``
    - because entering a breakpoint needs a C-side enter this layer has not got;
    see the module docstring of ``tests/bake_parity``.
    """

    def _broken(self, tag: str):
        """A root whose first arm breaks out of a group, and the breakpoint.

        A break needs an active node to take an arm from, so the group and the
        branch around it are part of the fixture rather than of the case.
        """
        from contextlib import contextmanager
        from decision_graph.decision_tree.bake.c_collections import LogicMapping
        from decision_graph.decision_tree.bake.c_const import ConstantNode
        from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator

        mapping = LogicMapping(name=tag)
        with RootLogicNode(name=f'{tag}r') as root:
            with mapping:
                # The read is built while the STORE is the active group: inside
                # the LogicGroup below, the active group is that group and there
                # is no store to read from.
                signal = mapping['a']
                with LogicGroup(name=f'{tag}g') as group:
                    with BinaryExpression(ExpressionOperator.gt, signal, ConstantNode(0)):
                        breakpoint = BreakpointNode.break_(break_from=group)
        return root, breakpoint

    def test_00_break_hands_back_the_block_that_landed(self) -> None:
        """The wrapper returned is the node in the graph, by address."""
        root, breakpoint = self._broken('br0')
        self.assertEqual(breakpoint.address, root.get_breakpoint().address)

    def test_01_a_fresh_breakpoint_is_waiting(self) -> None:
        """Nothing has resumed into it yet, so it says so and links to nothing."""
        root, breakpoint = self._broken('br1')
        self.assertTrue(breakpoint.await_connection)
        self.assertIsNone(breakpoint.linked_to)

    def test_02_connect_is_what_resumes_into_it(self) -> None:
        """The child comes back through ``linked_to`` and names the breakpoint."""
        root, breakpoint = self._broken('br2')

        child = RootLogicNode(name='br2child')
        breakpoint.connect(child)

        self.assertIs(breakpoint.linked_to, child)
        self.assertIs(child.parent, breakpoint)

    def test_03_get_breakpoint_finds_the_waiting_one(self) -> None:
        """The root reports the breakpoint the build left behind."""
        root, breakpoint = self._broken('br3')
        self.assertIs(root.get_breakpoint(), breakpoint)

    def test_04_a_root_with_no_break_leaves_none(self) -> None:
        """A graph that never broke has nothing waiting, and says None."""
        from decision_graph.decision_tree.bake.c_action import LongAction
        from decision_graph.decision_tree.bake.c_collections import LogicMapping
        from decision_graph.decision_tree.bake.c_const import ConstantNode
        from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator

        mapping = LogicMapping(name='br4')
        with RootLogicNode(name='br4r') as root:
            with mapping:
                with BinaryExpression(ExpressionOperator.gt, mapping['a'], ConstantNode(0)):
                    LongAction()
        self.assertIsNone(root.get_breakpoint())

    def test_05_inherit_contexts_is_reported_back(self) -> None:
        """The flag is a constructor argument and a field, read the same way."""
        self.assertFalse(RootLogicNode(name='br5a').inherit_contexts)
        self.assertTrue(RootLogicNode(name='br5b', inherit_contexts=True).inherit_contexts)


class TestEnteringABreakpoint(unittest.TestCase):
    """Contract: entering a breakpoint is the other way to resume into it.

    Expected behavior:
        - entering it TAKES IT OVER: the waiting flag comes off and the manager
          stops being the one to connect it;
        - the arm it opens is ONE, and it is the else - "the branch carried on
          here", not a value to compare against;
        - the build inside it is what it resumes into, and the arm is what the
          build fills;
        - entering one that already resumed is refused rather than given a
          second arm, because a breakpoint resumes into exactly one node;
        - once resumed it is no longer the graph's waiting breakpoint, so
          ``get_breakpoint`` stops reporting it.

    This is what makes the reference idiom work: one function builds up to a
    break and returns the root, and the caller enters that root's breakpoint and
    builds the rest - so the two halves never share a scope.
    """

    def _broken(self, tag: str):
        """A root with a break in its first arm, and the breakpoint."""
        from decision_graph.decision_tree.bake.c_collections import LogicMapping
        from decision_graph.decision_tree.bake.c_const import ConstantNode
        from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator

        mapping = LogicMapping(name=tag)
        with RootLogicNode(name=f'{tag}r') as root:
            with mapping:
                signal = mapping['a']
                with LogicGroup(name=f'{tag}g') as group:
                    with BinaryExpression(ExpressionOperator.gt, signal, ConstantNode(0)):
                        breakpoint = BreakpointNode.break_(break_from=group)
        return root, breakpoint, mapping

    def test_00_entering_takes_the_breakpoint_over(self) -> None:
        """The waiting flag comes off: the manager is no longer to connect it.

        The queue is read rather than deltas from it, because a build that has
        LEFT its root has none to read: the root's exit unshelves a state its
        enter never shelved, and the queue goes with it. That is a pre-existing
        asymmetry in the shelving - this layer's root reserves its arm and does
        not shelve - and it is why the count below is asserted as it stands
        rather than as a difference.
        """
        from decision_graph.decision_tree.bake.c_logic_group import LGM

        root, breakpoint, mapping = self._broken('en0')
        self.assertTrue(breakpoint.await_connection)
        self.assertEqual(LGM.n_breakpoints, 0)

        with breakpoint:
            self.assertFalse(breakpoint.await_connection)
            self.assertEqual(LGM.n_breakpoints, 0)

    def test_01_the_arm_it_opens_is_one_else(self) -> None:
        """One arm, and its condition is the else."""
        from decision_graph.decision_tree.bake.c_edge import ELSE_CONDITION

        root, breakpoint, mapping = self._broken('en1')
        with breakpoint:
            self.assertEqual(len(breakpoint.children), 1)
            # By VALUE, not identity: the arm's condition is the registry's
            # wrapper for the C sentinel, and two wrappers of one condition are
            # one edge - which is what lets ``children`` be keyed by them.
            self.assertIn(ELSE_CONDITION, breakpoint.children)

    def test_02_the_build_inside_is_what_it_resumes_into(self) -> None:
        """What is built inside the entered breakpoint becomes ``linked_to``."""
        from decision_graph.decision_tree.bake.c_const import ConstantNode
        from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator

        root, breakpoint, mapping = self._broken('en2')
        with breakpoint:
            with mapping:
                node = BinaryExpression(ExpressionOperator.gt, mapping['a'], ConstantNode(5))
                # Entering the node is what PLACES it: a node joins the arm the
                # active one has open, and inside the breakpoint the active one
                # is the breakpoint.
                with node:
                    pass
        self.assertIs(breakpoint.linked_to, node)
        self.assertIs(node.parent, breakpoint)

    def test_03_entering_one_that_resumed_is_refused(self) -> None:
        """A breakpoint resumes into one node; a second enter has no arm to open."""
        root, breakpoint, mapping = self._broken('en3')
        with breakpoint:
            pass

        with self.assertRaises(RuntimeError):
            breakpoint.__enter__()

    def test_04_a_resumed_breakpoint_is_no_longer_the_waiting_one(self) -> None:
        """The finder reports where a branch STOPPED, and this one carried on."""
        root, breakpoint, mapping = self._broken('en4')
        self.assertIs(root.get_breakpoint(), breakpoint)

        with breakpoint:
            pass
        self.assertIsNone(root.get_breakpoint())

    def test_05_the_split_build_bakes_and_decides(self) -> None:
        """The reference idiom end to end, with a plain node as the second half."""
        from decision_graph.decision_tree.bake.c_action import CancelAction, LongAction
        from decision_graph.decision_tree.bake.c_const import ConstantNode
        from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator

        root, breakpoint, mapping = self._broken('en5')

        with root.get_breakpoint():
            with mapping:
                more = BinaryExpression(ExpressionOperator.gt, mapping['a'], ConstantNode(5))
                with more:
                    LongAction()
                    CancelAction()

        self.assertIsNone(root.validate())
        self.assertEqual(root.bake().code_name, 'OK')

        with mapping:
            mapping['a'] = 9
            self.assertIsInstance(root(), LongAction)
            mapping['a'] = 1
            self.assertIsInstance(root(), CancelAction)
