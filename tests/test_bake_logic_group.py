"""Wrapper tests for ``decision_graph.decision_tree.bake.c_logic_group``.

Scope is the Python surface. What the manager's stacks do to a build is settled
by ``tests/bake/test_c_logic_group.c``; what is checked here is the wrapper's
half of the bargain:

  - a group knows its name and the group it hangs from;
  - a group is registered under its address, so a group reached from C comes
    back as the wrapper the build made rather than as a second view of it;
  - the manager finds a registered group by name, and reports the group a build
    is inside.

Oracle: the objects handed in and the C node's own reported name - never a
re-derivation of the wrapper's own bookkeeping.
"""

import itertools
import unittest
from contextlib import contextmanager

from decision_graph.decision_tree.bake.c_collections import LogicMapping
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode
from decision_graph.decision_tree.bake.c_logic_group import (
    GROUP_REGISTRY,
    LGM,
    LogicGroup,
)

# Break cases build a store of their own, and a store name is part of every
# read's display text - so these are short and counted, never derived from a
# test name.
_store_tag = 'lg'
_store_counter = itertools.count()


class GroupTestCase(unittest.TestCase):
    """A base that gives each test a scope of its own.

    A group registers itself with the manager under its NAME, and the manager
    lives for the process, so two tests building a group called "scope" would
    collide. Every name here is derived from the test that asked for it.
    """

    def setUp(self) -> None:
        self.scope = f'{type(self).__name__}.{self._testMethodName}'


class TestLogicGroup(GroupTestCase):
    """Contract: a group is a named scope, and it knows where it sits.

    Expected behavior:
        - a group reports the name it was built with;
        - with no name, it reports none;
        - a child reports the wrapper of its parent, not a second view of it.
    """

    def test_00_a_group_reports_its_name(self) -> None:
        """The name it was built with comes back."""
        self.assertEqual(LogicGroup(name=self.scope).name, self.scope)

    def test_01_a_nameless_group_reports_none(self) -> None:
        """A group without a name has nothing to report."""
        self.assertIsNone(LogicGroup().name)

    def test_02_a_group_has_an_address(self) -> None:
        """A group adopted a real block."""
        self.assertGreater(LogicGroup(name=self.scope).address, 0)

    def test_03_a_child_reports_its_parent(self) -> None:
        """The parent comes back as the wrapper that was handed in."""
        parent = LogicGroup(name=f'{self.scope}.outer')
        child = LogicGroup(name=f'{self.scope}.inner', parent=parent)
        self.assertIs(child.parent, parent)

    def test_04_a_rootless_group_has_no_parent(self) -> None:
        """A group that hangs from nothing reports no parent."""
        self.assertIsNone(LogicGroup(name=f'{self.scope}.alone').parent)


class TestGroupRegistry(GroupTestCase):
    """Contract: one block, one wrapper.

    Expected behavior:
        - a group is registered under its own address when it is built;
        - the entry is the very wrapper the caller holds;
        - a lookup with no entry rebuilds a view rather than failing.

    The registry is what lets a group reached from C - the active one, a parent -
    come back as the wrapper the build made: two wrappers of one group would be
    two things where the build sees one.
    """

    def test_00_a_group_is_registered_under_its_address(self) -> None:
        """Building a group registers it by address."""
        group = LogicGroup(name=self.scope)
        self.assertIs(GROUP_REGISTRY[group.address], group)

    def test_01_a_lookup_with_no_entry_rebuilds(self) -> None:
        """An address the registry does not hold is still answered."""
        group = LogicGroup(name=self.scope)
        address = group.address
        del GROUP_REGISTRY[address]

        rebuilt = GROUP_REGISTRY[address]
        self.assertIsInstance(rebuilt, LogicGroup)
        self.assertEqual(rebuilt.name, self.scope)

    def test_02_distinct_groups_occupy_distinct_entries(self) -> None:
        """Two groups do not share a key."""
        left = LogicGroup(name=f'{self.scope}.left')
        right = LogicGroup(name=f'{self.scope}.right')
        self.assertIs(GROUP_REGISTRY[left.address], left)
        self.assertIs(GROUP_REGISTRY[right.address], right)


class TestManagerLookups(GroupTestCase):
    """Contract: the manager answers for what is inside it.

    Expected behavior:
        - a group is found by the name it was built with;
        - a name already taken is refused rather than silently shadowing;
        - the group a build is inside is reported as the wrapper it entered;
        - with no build open, there is no active group.

    The two lookups answer different questions - by name is the registry, by
    position is the build - and the second has to preserve identity, because a
    group is what its nodes are labelled with.
    """

    def test_00_a_group_is_found_by_the_name_it_was_built_with(self) -> None:
        """Building a group is what registers it."""
        LogicGroup(name=self.scope)
        found = LGM.find(self.scope)
        self.assertIsInstance(found, LogicGroup)
        self.assertEqual(found.name, self.scope)

    def test_01_a_name_already_taken_is_refused(self) -> None:
        """One name, one group: a second one is reported, not shadowed."""
        LogicGroup(name=self.scope)
        with self.assertRaises(RuntimeError):
            LogicGroup(name=self.scope)

    def test_02_the_active_group_is_the_wrapper_that_was_entered(self) -> None:
        """Identity is kept: the build's own wrapper comes back."""
        group = LogicGroup(name=self.scope)
        with group:
            self.assertIs(LGM.active_group, group)

    def test_03_no_active_group_outside_a_build(self) -> None:
        """With nothing entered, there is no group to report."""
        self.assertIsNone(LGM.active_group)


class TestTheBlockBehindTheWrapper(GroupTestCase):
    """Contract: a group wrapper may outlive the block it stands for, as a husk.

    Expected behavior:
        - a block released under a live wrapper leaves that wrapper with no
          address, no name, no type and no parent - the accessors refuse rather
          than read a block that is gone;
        - ``repr`` still answers, because it is the surface a debugger reads;
        - a husk releases nothing a second time, and dies quietly;
        - ``GROUP_REGISTRY`` does not answer an address with a husk: the entry is
          dropped and the block itself is asked what stands there now.

    A group block is released by the wrapper that owns it, and by that one only,
    so the husk is the owner's own release seen from the other wrappers: the
    name registry's view and any view a lookup rebuilt. What makes that safe is
    the allocator's binding: every wrapper holds a reference on its block, the
    block's FREE pass runs before it is reclaimed, and the wrapper is
    invalidated there. This is what the CCP protocol is for, and these are the
    terms of it a caller can see.
    """

    def _husked_pair(self) -> tuple:
        """An owned block and a second wrapper over it, freed under the second.

        The registry answers a held group with the wrapper the layer already
        has, so the entry is dropped before the lookup: that is what makes the
        lookup build a second wrapper over the same block instead of handing
        back the first one.
        """
        # A store name is part of every read's display text, so it is short and
        # counted: the class-and-method scope this base hands out is far too long.
        self.group_name = f'{_store_tag}{next(_store_counter)}'
        owner = LogicMapping(name=self.group_name)
        address = owner.address
        del GROUP_REGISTRY[address]

        view = GROUP_REGISTRY[address]
        return owner, view, address

    def test_00_a_released_block_leaves_a_husk(self) -> None:
        """The wrapper of a block the layer releases stops reporting it."""
        owner, view, address = self._husked_pair()
        self.assertIsNot(view, owner)
        self.assertEqual(view.address, address)  # one block, two wrappers
        self.assertEqual(view.name, self.group_name)

        del owner  # the block is released while the view still names it

        # Only the property accessors are guarded. A method called on a husk is
        # the caller's mistake: it reaches the block that is gone, and that is
        # the caller's to avoid. ``address`` reports the absence as 0.
        self.assertEqual(view.address, 0)
        for accessor in ('name', 'parent', 'lgtype'):
            with self.subTest(accessor=accessor):
                with self.assertRaises(RuntimeError):
                    getattr(view, accessor)

    def test_01_a_husk_releases_nothing_a_second_time(self) -> None:
        """Deallocating a husk is silent: the block is already gone.

        A second free would trip the allocator's own vigilant check and abort
        the process, so reaching the end of this test IS the assertion.
        """
        owner, view, address = self._husked_pair()
        del owner

        del view
        self.assertEqual(LogicGroup(name=f'{self.group_name}.after').name, f'{self.group_name}.after')

    def test_02_a_lookup_answers_with_the_entry_and_never_writes(self) -> None:
        """A read answers with the entry it holds; it does not mutate the map.

        The entry is put back by hand after the block is gone - a state the
        layer does not leave behind. A lookup returns it as it stands and
        leaves the map alone; ``address`` is what tells a husk from a live
        wrapper (0 versus the block's address).
        """
        owner, husk, address = self._husked_pair()
        del owner

        self.assertEqual(husk.address, 0)  # a husk has no address

        GROUP_REGISTRY[address] = husk  # the entry a lookup finds
        answered = GROUP_REGISTRY[address]

        self.assertIs(answered, husk)
        self.assertIn(address, GROUP_REGISTRY)

    def test_03_an_owning_mapping_still_frees_normally(self) -> None:
        """A held mapping owns its block, and letting it go frees that block.

        The registry holds the wrapper itself, so the entry goes first -
        otherwise the map, not the test, would be the last owner. Under the
        vigilant allocator a block that still had a bound wrapper aborts the
        process on free, so reaching the next line IS the assertion.
        """
        # Short and counted, for the reason ``_husked_pair`` gives.
        self.group_name = f'{_store_tag}{next(_store_counter)}'
        mapping = LogicMapping(name=self.group_name)
        mapping['entry'] = 1.5
        address = mapping.address
        del GROUP_REGISTRY[address]

        del mapping
        self.assertEqual(LogicGroup(name=f'{self.group_name}.after').name, f'{self.group_name}.after')


class TestTheBreakDoors(GroupTestCase):
    """Contract: a break NAMES the group it breaks - the instance door does.

    Expected behavior:
        - ``break_`` is an instance method: it breaks the group it is asked of,
          or the scope it is handed;
        - ``break_active`` is a classmethod taking the scope to break, and with
          no scope nothing is named, so nothing happens;
        - a break takes the active node's arm, which is what the count of queued
          breakpoints reports: with a branch entered the count rises by one.

    This deliberately does NOT match the capi, whose ``LogicGroup.break_`` is a
    classmethod falling back to the active group. Naming the group at the call
    is the modular control this layer wants: which group a break leaves is read
    off the call rather than off whatever the build happens to be inside, so a
    caller assembling a graph from several places - a sub-graph per function,
    joined by breaks - never has to know the ambient scope. The cost is that a
    bare ``break_()`` needs a receiver, which is the trade this design makes.

    What a break does to the GRAPH - it swaps the active node's reserved arm for
    a breakpoint naming the group - is ``tests/bake/test_c_logic_group.c``'s
    subject; what is checked here is the door and the arm it took.
    """

    @contextmanager
    def _entered_branch(self, tag: str):
        """A root and one branch entered, so a break has an arm to take.

        A break needs an active NODE: it is the node's arm that becomes the
        breakpoint. Nothing is evaluated - entering the branch is all this does,
        and it is left again on the way out so the next case starts clean.
        """
        # A mapping and a root each register under their own name, so the two
        # cannot share one: a name is taken once, and a second group under it is
        # refused rather than shadowing the first.
        mapping = LogicMapping(name=f'{tag}m')
        with RootLogicNode(name=f'{tag}r') as root:
            with mapping:
                branch = BinaryExpression(ExpressionOperator.gt, mapping['signal'], ConstantNode(0))
                with branch:
                    yield root, branch

    def test_00_break_is_asked_of_an_instance(self) -> None:
        """The group a break leaves is the receiver, or the scope handed to it."""
        import inspect

        self.assertNotIsInstance(inspect.getattr_static(LogicGroup, 'break_'), classmethod)

    def test_01_break_active_is_asked_of_the_class(self) -> None:
        """A classmethod taking the scope, so the target is always a parameter."""
        import inspect

        self.assertIsInstance(inspect.getattr_static(LogicGroup, 'break_active'), classmethod)

    def test_02_a_break_takes_the_active_node_arm(self) -> None:
        """Asking the receiver to break queues one breakpoint."""
        tag = f'{_store_tag}{next(_store_counter)}'
        group = LogicGroup(name=tag)

        with self._entered_branch(tag):
            before = LGM.n_breakpoints
            with group:
                group.break_()
            self.assertEqual(LGM.n_breakpoints, before + 1)

    def test_03_a_break_can_name_a_scope_that_is_not_the_receiver(self) -> None:
        """A scope handed in is the group broken, not the one the build is inside."""
        tag = f'{_store_tag}{next(_store_counter)}'
        target = LogicGroup(name=f'{tag}t')
        inside = LogicGroup(name=f'{tag}i')

        with self._entered_branch(tag):
            before = LGM.n_breakpoints
            with inside:
                inside.break_(target)
            self.assertEqual(LGM.n_breakpoints, before + 1)

    def test_04_break_active_with_no_scope_names_nothing(self) -> None:
        """No scope is no group to break: the call is a no-op, not an error."""
        tag = f'{_store_tag}{next(_store_counter)}'

        with self._entered_branch(tag):
            before = LGM.n_breakpoints
            LogicGroup.break_active()
            self.assertEqual(LGM.n_breakpoints, before)

    def test_05_break_active_breaks_the_scope_it_is_given(self) -> None:
        """Asked of the class, the group it breaks is the argument."""
        tag = f'{_store_tag}{next(_store_counter)}'
        group = LogicGroup(name=tag)

        with self._entered_branch(tag):
            before = LGM.n_breakpoints
            LogicGroup.break_active(group)
            self.assertEqual(LGM.n_breakpoints, before + 1)
