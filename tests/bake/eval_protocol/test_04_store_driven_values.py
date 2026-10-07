"""The store drives the value: one graph, many writes, a new answer every time.

The graph is written ONCE and the store is what changes. Nothing about the nodes
is rebuilt and nothing is cached between evaluations: a read answers with what its
entry holds at the moment it is read, so the same expression node reports a
different value after every write, and the walk down a branch takes a different
arm.

What is pinned is the TYPE, and that is the second half of this file. A read
resolves to its entry's type at its FIRST evaluation - it stops holding the
entry's offset and starts holding the entry - and from then on it reports the
entry through the tag it resolved to. Writing a value of another type into that
entry does not convert anything: the read keeps its tag, which is the store's
contract (an entry keeps its type) rather than the read's promise.
"""

import unittest

from decision_graph.decision_tree.bake.c_expr import ExpressionOperator

from eval_case import EvalCase


class TestLiveReads(EvalCase):
    """A read reports the entry's value of the moment, at any depth."""

    def test_00_the_read_holds_the_entry_not_a_copy(self) -> None:
        read = self.read('x')
        self.assertTrue(read.out.is_null)  # nothing has landed yet
        self.assertEqual(read.out.type_name, 'inferred')  # the entry, and no type of its own

        self.fill(x=7)
        self.assertEqual(read.eval(), 7)
        self.assertTrue(read.out.is_ref)
        self.assertEqual(read.out.type_name, 'int_ref')
        self.assertEqual(read.out.value, 7)  # read through the reference

        self.fill(x=8)
        self.assertEqual(read.eval(), 8)  # same node, the store's new value
        self.assertEqual(read.out.value, 8)

    def test_01_one_graph_many_writes(self) -> None:
        read = self.read('x')
        scaled = (read + self.const(1)) * self.const(2)

        for value in (0, 1, 5, -3, 100):
            with self.subTest(x=value):
                self.fill(x=value)
                self.assertEqual(scaled.eval(), (value + 1) * 2)
                self.show(f'x={value:<4}', 'scaled', scaled.eval())

    def test_02_the_value_of_every_type_an_entry_can_hold(self) -> None:
        """Whatever was written is what comes back, through the tag the entry took.

        One entry per type: an entry's type is the one its first value gave it, and
        a value of another type is refused (see the retype cases below).
        """
        cases = ((True, 'bool_ref', True), (False, 'bool_ref', False), (3, 'int_ref', 3), (2.5, 'double_ref', 2.5), ('text', 'string_ref', 'text'))
        for index, (value, tag, expected) in enumerate(cases):
            with self.subTest(value=value):
                key = f'x{index}'
                self.fill(**{key: value})
                read = self.read(key)  # a read resolves to the type the entry has NOW
                self.assertEqual(read.eval(), expected)
                self.assertEqual(read.out.type_name, tag)
                self.show(f'{tag:<12}', read.eval())

    def test_03_a_read_resolves_once_and_keeps_the_fast_path(self) -> None:
        read = self.read('x')
        self.fill(x=4)
        self.assertEqual(read.eval(), 4)
        resolved = read.out.type_name
        for _ in range(3):
            self.assertEqual(read.eval(), 4)
            self.assertEqual(read.out.type_name, resolved)

    def test_04_a_word_of_another_type_is_refused(self) -> None:
        """The entry keeps its type, which is what a resolved read stands on.

        A read's fast path holds a reference that PROMISES the entry's type, and
        the store is what keeps that promise: a value of another type is refused,
        so the read never finds a slot that says one thing and holds another.
        """
        read = self.read('x')
        self.fill(x=7)
        self.assertEqual(read.eval(), 7)
        self.assertEqual(read.out.type_name, 'int_ref')

        with self.assertRaises(RuntimeError) as caught:
            self.fill(x=2.5)
        self.assertIn('-8', str(caught.exception))  # DCG_ERR_TYPE, as the store reports it

        self.assertEqual(read.eval(), 7)  # the entry is as it was
        self.assertEqual(read.out.type_name, 'int_ref')

        self.fill(x=8)  # its own type, a new value
        self.assertEqual(read.eval(), 8)
        self.show('an int entry after a double was offered', read.eval())

    def test_05_a_string_entry_retyped_under_a_read(self) -> None:
        """The case that used to be a crash: a resolved string read, offered a double.

        A read resolved to `string_ref` holds the address of the entry's payload.
        A double landing there would leave the read pointing at a double while
        saying "string", and reading it handed a pointer nothing allocated to the
        next reader that dereferenced it. The refusal is what makes that
        unreachable.
        """
        read = self.read('s')
        self.fill(s='text')
        self.assertEqual(read.eval(), 'text')
        self.assertEqual(read.out.type_name, 'string_ref')

        with self.assertRaises(RuntimeError):
            self.fill(s=3.5)  # a number does not land in a string entry
        self.assertEqual(read.eval(), 'text')  # and the read reads what it always read
        self.assertEqual(read.out.type_name, 'string_ref')

        self.fill(s='again')
        self.assertEqual(read.eval(), 'again')
        self.show('a string entry after a double was offered', read.eval())

    def test_05_a_frozen_store_still_answers(self) -> None:
        """Freezing seals the store's SHAPE, not its contents."""
        read = self.read('x')
        self.fill(x=5)
        self.assertEqual(read.eval(), 5)

        with self.mapping:
            self.mapping.frozen = True
            with self.assertRaises(KeyError) as caught:
                self.mapping['brand_new']
            self.assertIn('frozen', str(caught.exception))

        self.fill(x=6)
        self.assertEqual(read.eval(), 6)  # an entry it already holds still works
        self.assertIs(read.logic_group, self.mapping)


class TestTheStoreAndTheGraph(EvalCase):
    """What a graph does when its store grows, and when it is asked twice."""

    def test_00_a_read_built_before_a_growth_still_finds_its_entry(self) -> None:
        """The read holds the entry's OFFSET until it resolves, so growth is harmless."""
        read = self.read('early')
        self.fill(early=1)
        for index in range(20):
            self.fill(**{f'later{index}': index})

        self.assertEqual(read.eval(), 1)
        self.show('after 20 more entries', read.eval())

    def test_01_a_read_resolves_against_the_entry_of_the_moment_of_its_eval(self) -> None:
        read = self.read('x')
        first = self.read('x')
        self.fill(x=1)
        self.assertEqual(first.eval(), 1)
        self.fill(x=2)
        self.assertEqual(read.eval(), 2)
        self.assertEqual(first.eval(), 2)  # both read the same entry

    def test_02_a_graph_over_two_entries_of_one_store(self) -> None:
        total = self.read('a') + self.read('b')
        self.fill(a=1, b=2)
        self.assertEqual(total.eval(), 3)
        self.fill(b=20)
        self.assertEqual(total.eval(), 21)
        self.show('a=1 b=20', total.eval())


if __name__ == '__main__':
    unittest.main()
