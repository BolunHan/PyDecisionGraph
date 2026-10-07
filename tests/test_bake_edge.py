"""Wrapper tests for ``decision_graph.decision_tree.bake.c_edge``.

Scope is the Python surface only. What the C predicates mean is settled by
``tests/bake/test_c_edge.c``; what is checked here is that the wrapper hands
them to Python without changing what they say - that the five built-ins are
recognisable singletons, that a caller's own condition survives a round trip,
and that equality and hashing agree with the type field rather than with an
address.

Oracle: the expected kind of each built-in comes from the class it was built
as, never from re-running the predicate under test.
"""

import unittest

from decision_graph.decision_tree.bake import c_edge


class TestBuiltinConditions(unittest.TestCase):
    """Contract: the five built-ins are singletons built once at import.

    Expected behavior:
        - each reports exactly the predicate its class names, and no other;
        - a built-in carries the boolean its name promises;
        - they are distinct objects, and five distinct hash keys.

    The five exist so that an edge the C layer made on its own can be handed
    back as the same object every time, which is what lets a caller key a
    ``children`` map by them.
    """

    def test_00_each_builtin_answers_its_own_predicate(self) -> None:
        """Each singleton reports true for its own predicate and false for the rest."""
        self.assertTrue(c_edge.NO_CONDITION.is_none)
        self.assertFalse(c_edge.NO_CONDITION.is_else)
        self.assertFalse(c_edge.NO_CONDITION.is_auto)

        self.assertTrue(c_edge.ELSE_CONDITION.is_else)
        self.assertFalse(c_edge.ELSE_CONDITION.is_none)
        self.assertFalse(c_edge.ELSE_CONDITION.is_auto)

        self.assertTrue(c_edge.AUTO_CONDITION.is_auto)
        self.assertFalse(c_edge.AUTO_CONDITION.is_none)
        self.assertFalse(c_edge.AUTO_CONDITION.is_else)

    def test_01_each_builtin_has_the_class_it_was_built_as(self) -> None:
        """The class is the independent oracle for which built-in this is."""
        self.assertIsInstance(c_edge.NO_CONDITION, c_edge.ConditionAny)
        self.assertIsInstance(c_edge.ELSE_CONDITION, c_edge.ConditionElse)
        self.assertIsInstance(c_edge.AUTO_CONDITION, c_edge.ConditionAuto)
        self.assertIsInstance(c_edge.TRUE_CONDITION, c_edge.ConditionTrue)
        self.assertIsInstance(c_edge.FALSE_CONDITION, c_edge.ConditionFalse)

    def test_02_true_and_false_carry_their_boolean(self) -> None:
        """The two binary built-ins convert to the boolean they name."""
        self.assertTrue(bool(c_edge.TRUE_CONDITION))
        self.assertFalse(bool(c_edge.FALSE_CONDITION))
        self.assertEqual(int(c_edge.TRUE_CONDITION), 1)
        self.assertEqual(int(c_edge.FALSE_CONDITION), 0)

    def test_03_builtins_are_distinct_objects(self) -> None:
        """No two of the five are the same wrapper."""
        builtins = (
            c_edge.NO_CONDITION,
            c_edge.ELSE_CONDITION,
            c_edge.AUTO_CONDITION,
            c_edge.TRUE_CONDITION,
            c_edge.FALSE_CONDITION,
        )
        for i, left in enumerate(builtins):
            for right in builtins[i + 1:]:
                with self.subTest(left=left, right=right):
                    self.assertIsNot(left, right)

    def test_04_builtins_hash_to_distinct_keys(self) -> None:
        """The five are usable as five keys in one dict.

        A sentinel hashes by its type field, so five distinct kinds have to
        give five distinct keys - otherwise a children map would collide.
        """
        builtins = (
            c_edge.NO_CONDITION,
            c_edge.ELSE_CONDITION,
            c_edge.AUTO_CONDITION,
            c_edge.TRUE_CONDITION,
            c_edge.FALSE_CONDITION,
        )
        self.assertEqual(len({hash(c) for c in builtins}), 5)

    def test_05_a_builtin_equals_itself_and_not_another(self) -> None:
        """Equality is decided by the type field, so it is reflexive and kind-bound."""
        self.assertEqual(c_edge.NO_CONDITION, c_edge.NO_CONDITION)
        self.assertNotEqual(c_edge.NO_CONDITION, c_edge.AUTO_CONDITION)
        self.assertNotEqual(c_edge.TRUE_CONDITION, c_edge.FALSE_CONDITION)

    def test_06_str_is_the_conditions_own_text(self) -> None:
        """``str()`` is the condition's canonical text, not a class label.

        The text comes from the C formatter - what a render or a log line shows.
        Which Python class happens to wrap it is not part of it; ``repr()`` is
        where the class name lives.
        """
        self.assertIn('Unconditional', str(c_edge.NO_CONDITION))
        self.assertNotIn('ConditionAny', str(c_edge.NO_CONDITION))

    def test_07_repr_names_the_class_and_the_text(self) -> None:
        """``repr()`` wraps the same text in the wrapper's class name."""
        self.assertIn('ConditionAny', repr(c_edge.NO_CONDITION))
        self.assertIn('ConditionElse', repr(c_edge.ELSE_CONDITION))


class TestUserCondition(unittest.TestCase):
    """Contract: a caller's condition is built from a Python value and a repr.

    Expected behavior:
        - a value of any of the packed types is accepted;
        - None is refused, because the five built-ins cover the valueless edges;
        - the result is none of the built-in kinds;
        - two conditions carrying the same value compare equal, and different
          ones do not.

    Oracle: equality is asserted between two independently constructed
    conditions, so it tests the comparison rather than the constructor.
    """

    def test_00_a_value_condition_is_built_and_is_not_a_builtin(self) -> None:
        """A condition carrying a value answers false to every built-in predicate."""
        condition = c_edge.NodeEdgeCondition(7, 'seven')
        self.assertFalse(condition.is_none)
        self.assertFalse(condition.is_else)
        self.assertFalse(condition.is_auto)

    def test_01_a_value_condition_is_not_one_of_the_builtins(self) -> None:
        """It is a plain NodeEdgeCondition, not any of the five subclasses."""
        condition = c_edge.NodeEdgeCondition(7, 'seven')
        self.assertIs(type(condition), c_edge.NodeEdgeCondition)

    def test_02_each_packed_type_is_accepted(self) -> None:
        """The types the C packer knows all build a condition."""
        for value in (True, 3, 2.5, 'text'):
            with self.subTest(value=value):
                condition = c_edge.NodeEdgeCondition(value, repr(value))
                self.assertIsNotNone(condition)

    def test_03_none_is_refused(self) -> None:
        """A valueless condition is one of the built-ins, not a new object."""
        with self.assertRaises(ValueError) as ctx:
            c_edge.NodeEdgeCondition(None)
        self.assertIn('five built-ins', str(ctx.exception))

    def test_04_equal_values_compare_equal(self) -> None:
        """Two conditions built from the same value are equal."""
        left = c_edge.NodeEdgeCondition(11, 'eleven')
        right = c_edge.NodeEdgeCondition(11, 'eleven')
        self.assertEqual(left, right)

    def test_05_different_values_compare_unequal(self) -> None:
        """Different payloads are different conditions."""
        left = c_edge.NodeEdgeCondition(11, 'eleven')
        right = c_edge.NodeEdgeCondition(12, 'twelve')
        self.assertNotEqual(left, right)

    def test_06_comparison_against_a_non_condition_is_a_type_error(self) -> None:
        """Comparing with something that is not a condition is refused.

        The operand is typed, so handing it a foreign object is a programming
        error the wrapper reports - rather than a comparison it quietly answers
        False to, which would hide the mistake at the call site.
        """
        condition = c_edge.NodeEdgeCondition(11, 'eleven')
        with self.assertRaises(TypeError):
            _ = condition == 11
        with self.assertRaises(TypeError):
            _ = condition != object()

    def test_07_a_condition_renders_its_own_text(self) -> None:
        """A user condition stringifies to the text it was built with."""
        condition = c_edge.NodeEdgeCondition(11, 'eleven')
        self.assertEqual(str(condition), 'eleven')
        self.assertIn('NodeEdgeCondition', repr(condition))

class TestEdgeRegistryLookup(unittest.TestCase):
    """Contract: the registry answers for an ADDRESS with the class that address is.

    Expected behavior:
        - a condition the registry does not hold comes back as the class its own
          TYPE names, built over the address that was asked for - not as a shared
          instance of that class, and not as the base class;
        - the five built-ins keep coming back as themselves, because those are
          registered.
    """

    def test_00_a_missing_condition_comes_back_as_its_own_type(self) -> None:
        """The dispatch reads the edge's type, and the instance is at the address."""
        for sentinel, cls in (
            (c_edge.TRUE_CONDITION, c_edge.ConditionTrue),
            (c_edge.FALSE_CONDITION, c_edge.ConditionFalse),
            (c_edge.AUTO_CONDITION, c_edge.ConditionAuto),
            (c_edge.ELSE_CONDITION, c_edge.ConditionElse),
            (c_edge.NO_CONDITION, c_edge.ConditionAny),
        ):
            with self.subTest(condition=cls.__name__):
                address = sentinel.address
                del c_edge.EDGE_REGISTRY[address]
                rebuilt = c_edge.EDGE_REGISTRY[address]

                self.assertIs(type(rebuilt), cls)
                self.assertEqual(rebuilt.address, address)   # the address asked for, not another
                self.assertEqual(rebuilt, sentinel)          # and the same edge

    def test_01_a_user_condition_comes_back_as_the_base_class(self) -> None:
        """A keyed condition has no class of its own: the base is what it is.

        Nothing to drop for this one: the registry holds the five built-ins, and a
        condition a caller made is the caller's object. A lookup by its address is
        the missing case already.
        """
        condition = c_edge.NodeEdgeCondition(11, 'eleven')
        address = condition.address

        rebuilt = c_edge.EDGE_REGISTRY[address]
        self.assertIs(type(rebuilt), c_edge.NodeEdgeCondition)
        self.assertEqual(rebuilt.address, address)


class TestConditionValue(unittest.TestCase):
    """Contract: an edge says what value it is taken by, or refuses to say.

    Expected behavior:
        - a caller's condition answers with the Python object it was built with,
          whatever simple tag that object is;
        - the two arms of a branch answer with ``True`` and ``False`` - which is
          what they ARE - so a built-in edge is usable without unpacking the C
          tag first;
        - the three edges that stand for "no particular value" - unconditional,
          else, auto - hold none, and say so by raising rather than by answering
          with a value that says nothing.

    The capi is the oracle for that last part, and it refuses too: its
    ``ConditionAny.value`` and ``ConditionElse.value`` are not readable, and its
    ``ConditionAuto.value`` raises. What bake does not copy is the SHAPE of the
    refusal - all three refuse one way here, because capi's ``AttributeError``
    comes from a property written with no getter rather than from a decision.
    """

    def test_00_a_caller_condition_answers_with_its_value(self) -> None:
        """The value it was built with is the value it reports."""
        condition = c_edge.NodeEdgeCondition(11, 'eleven')
        self.assertEqual(condition.value, 11)

    def test_01_a_value_of_every_simple_tag_round_trips(self) -> None:
        """Each tag the layer can pack comes back as the object it was."""
        for value in (True, 3, 2.5, 'text'):
            with self.subTest(value=value):
                self.assertEqual(c_edge.NodeEdgeCondition(value, None).value, value)

    def test_02_the_two_arms_answer_with_what_they_are(self) -> None:
        """The true arm is True and the false arm is False."""
        self.assertIs(c_edge.TRUE_CONDITION.value, True)
        self.assertIs(c_edge.FALSE_CONDITION.value, False)

    def test_03_the_edges_that_hold_no_value_refuse(self) -> None:
        """Unconditional, else and auto all report that they have no value."""
        for condition in (c_edge.NO_CONDITION, c_edge.ELSE_CONDITION, c_edge.AUTO_CONDITION):
            with self.subTest(condition=str(condition)):
                with self.assertRaises(ValueError):
                    condition.value
