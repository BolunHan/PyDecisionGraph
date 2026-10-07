"""Wrapper tests for ``decision_graph.decision_tree.bake.c_collections``.

Scope is the Python surface. What the store does to its slots is settled by
``tests/bake/test_c_collection.c``; what is checked here is the wrapper's half of
the bargain:

  - a mapping is a group, so a build can enter it;
  - reading is how entries come to exist - the reservation protocol - so a read
    is never a KeyError against a store that is still being filled;
  - a read follows its entry: it is answered by the store, so it reports the
    value AND the type of the moment, whatever the entry was when it was built;
  - a frozen mapping keeps the entries it has and gives out no new ones.

A read is built inside the group it reads - that is what makes it the store of
the build - so the tests here enter the mapping to read through it.

Oracle: the values written, read back through the read that was built for them,
and the capi's own mapping - the same graph built against it - for the parts both
layers share.
"""

import itertools
import unittest

from decision_graph.decision_tree.bake.c_collections import AttrExpression, LogicMapping
from decision_graph.decision_tree.bake.c_const import ConstantNode, VariableNode
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_logic_group import LGM, LogicGroup
from decision_graph.decision_tree.bake.c_node import NODE_REGISTRY
from decision_graph.decision_tree.bake.c_var import VarType

# Store names are short and unique: a name is part of every read's display text.
_store_tag = 'mc'
_store_counter = itertools.count()


class MappingTestCase(unittest.TestCase):
    """A base that gives each test a store of its own.

    A group registers itself with the manager under its NAME, and the manager
    lives for the process, so two tests building a store called "store" would
    collide. Every name here is derived from the test that asked for it.
    """

    def setUp(self) -> None:
        # Short, because a store's name is part of every read's display text and
        # the layer refuses one long enough to overflow the repr buffer.
        self.scope = f'{_store_tag}{next(_store_counter)}'


class TestLogicMapping(MappingTestCase):
    """Contract: a mapping is a group that holds values by name.

    Expected behavior:
        - it is a group: it carries a name, an address and can be entered;
        - a fresh mapping holds nothing;
        - a value written to an entry reads back through the read of that entry.
    """

    def test_00_a_mapping_is_a_group(self) -> None:
        """The store is a scope a build runs inside, not a node."""
        mapping = LogicMapping(name=self.scope)
        self.assertIsInstance(mapping, LogicGroup)
        self.assertEqual(mapping.name, self.scope)
        self.assertGreater(mapping.address, 0)

    def test_01_a_fresh_mapping_holds_nothing(self) -> None:
        """No entries until one is written or read."""
        mapping = LogicMapping(name=self.scope)
        self.assertEqual(len(mapping), 0)
        self.assertNotIn('close', mapping)

    def test_02_a_written_entry_reads_back(self) -> None:
        """A value put in an entry comes back through its read."""
        mapping = LogicMapping(name=self.scope)
        mapping['close'] = 12.5
        with mapping:
            read = mapping['close']
        self.assertEqual(len(mapping), 1)
        self.assertIn('close', mapping)
        self.assertEqual(read.value, 12.5)

    def test_03_every_packed_type_round_trips(self) -> None:
        """The literal kinds a slot can hold all survive the store."""
        mapping = LogicMapping(name=self.scope)
        for key, value in (('an_int', 7), ('a_float', 2.5), ('a_bool', True), ('a_string', 'text')):
            with self.subTest(entry=key):
                mapping[key] = value
                with mapping:
                    self.assertEqual(mapping[key].value, value)

    def test_04_a_slot_keeps_the_type_of_what_it_holds(self) -> None:
        """Writing replaces the VALUE: the type an entry took is the entry's.

        A read resolves to its entry's type once and then holds a reference that
        promises it, so the store refuses a value of another type - which is what
        makes that promise one a read can stand on (see
        DCG_MAPPING_IMMUTABLE_DTYPE).
        """
        mapping = LogicMapping(name=self.scope)
        mapping['x'] = 7
        with mapping:
            read = mapping['x']
        self.assertEqual(read.value, 7)

        mapping['x'] = 8  # the same type, a new value
        self.assertEqual(read.value, 8)

        with self.assertRaises(RuntimeError) as caught:
            mapping['x'] = 'seven'  # another type: refused
        self.assertIn('-8', str(caught.exception))  # DCG_ERR_TYPE, as the store reports it
        self.assertEqual(read.value, 8)  # and the read still reads what the entry holds

    def test_05_the_attribute_form_is_the_same_read(self) -> None:
        """``mapping.close`` is the read ``mapping['close']`` is."""
        mapping = LogicMapping(name=self.scope)
        mapping['close'] = 1.5
        with mapping:
            self.assertEqual(mapping.close.value, 1.5)

    def test_06_two_reads_of_one_entry_read_the_same_slot(self) -> None:
        """A read is a view of the slot, so two of them see one value."""
        mapping = LogicMapping(name=self.scope)
        mapping['close'] = 4.0
        with mapping:
            first = mapping['close']
            second = mapping.close
        first_value = first.value
        mapping['close'] = 9.0
        self.assertEqual(first_value, 4.0)
        self.assertEqual(second.value, 9.0)

    def test_07_a_resolved_read_answers_from_where_it_resolved(self) -> None:
        """Evaluating a read is what turns its offset into the entry itself.

        Before that the read holds where its entry is; after it, the slot IS the
        entry, read through - so the value is still the live one, and the type is
        the one the entry held at the moment of the resolution.
        """
        mapping = LogicMapping(name=self.scope)
        mapping['x'] = 7
        with mapping:
            read = mapping['x']

        self.assertEqual(read.value, 7)   # unresolved: the offset is what finds the entry
        self.assertEqual(read.eval(), 7)  # and evaluating is what resolves it
        self.assertTrue(read.out.is_ref)  # from here the slot is the entry, read through
        self.assertIs(read.out.ref_base, VarType.int)

        mapping['x'] = 9                  # the type it resolved to: the value behind it is live
        self.assertEqual(read.value, 9)
        self.assertEqual(read.eval(), 9)


class TestReservationProtocol(MappingTestCase):
    """Contract: a read reserves the entry it names.

    Expected behavior:
        - reading an entry that is not held creates it, empty;
        - the read points at the slot the value will land in, so a value written
          afterwards is seen by a read built before it;
        - the reserved entry is an entry: it counts, and it is found again.
    """

    def test_05_the_value_brings_the_type_with_it(self) -> None:
        """An entry is typed by the value that lands in it, and the read reports it.

        A reserved entry has no type - it holds nothing - while the read made
        over it was built before either existed. The read is answered by the
        store, so the type that arrived is the type that is read.
        """
        mapping = LogicMapping(name=self.scope)
        with mapping:
            read = mapping['later']
            self.assertIsNone(read.value)  # reserved: no value, and no type

        mapping['later'] = 'text'
        self.assertEqual(read.value, 'text')

        with self.assertRaises(RuntimeError):
            mapping['later'] = 4.5  # a third type: the entry is a string entry now

        mapping['later'] = 'again'
        self.assertEqual(read.value, 'again')


    def test_00_a_read_reserves_the_entry(self) -> None:
        """Reading is how an entry comes to exist."""
        mapping = LogicMapping(name=self.scope)
        with mapping:
            read = mapping['later']
        self.assertIn('later', mapping)
        self.assertEqual(len(mapping), 1)
        self.assertIsNone(read.value)  # reserved, and nothing in it yet

    def test_01_a_read_built_before_the_value_sees_it(self) -> None:
        """The binding is a reference: the value arrives where the read looks."""
        mapping = LogicMapping(name=self.scope)
        with mapping:
            read = mapping['close']
        mapping['close'] = 3.25
        self.assertEqual(read.value, 3.25)

    def test_02_reading_twice_reserves_once(self) -> None:
        """Two reads of one entry are two nodes over ONE slot."""
        mapping = LogicMapping(name=self.scope)
        with mapping:
            first = mapping['close']
            second = mapping['close']
        self.assertEqual(len(mapping), 1)

        mapping['close'] = 8.0
        self.assertEqual(first.value, 8.0)   # one entry, and both reads see it
        self.assertEqual(second.value, 8.0)
        self.assertIsNot(first, second)      # a read is a node, not a cached view

    def test_03_the_reserved_entry_is_a_real_entry(self) -> None:
        """It is found by name, and by the attribute form."""
        mapping = LogicMapping(name=self.scope)
        with mapping:
            mapping['open']
        self.assertIn('open', mapping)
        with mapping:
            self.assertIsNone(mapping.open.value)

    def test_04_reserving_does_not_disturb_what_is_there(self) -> None:
        """A reservation adds an entry; the others keep their slots and values."""
        mapping = LogicMapping(name=self.scope)
        mapping['close'] = 1.0
        with mapping:
            read = mapping['close']
            mapping['open']  # reserved after the fact
        self.assertEqual(read.value, 1.0)
        self.assertEqual(len(mapping), 2)


class TestFrozenMapping(MappingTestCase):
    """Contract: freezing seals the store's shape, not its contents.

    Expected behavior:
        - a new entry cannot be created, by a read or by a write;
        - an entry the mapping holds is read and written as before;
        - a read that cannot be answered is reported as the miss it is.

    A frozen group is a store closed for business: nothing new appears in it, and
    what is in it keeps working.
    """

    def test_00_a_fresh_mapping_is_not_frozen(self) -> None:
        """Sealing is something a caller asks for."""
        self.assertFalse(LogicMapping(name=self.scope).frozen)

    def test_01_a_new_entry_is_refused_with_a_key_error(self) -> None:
        """A read that cannot be answered is the miss it is."""
        mapping = LogicMapping(name=self.scope)
        mapping.frozen = True
        with mapping:
            with self.assertRaises(KeyError):
                mapping['later']

    def test_02_a_new_entry_is_refused_by_a_write_too(self) -> None:
        """A write cannot create one either: creation is refused at its source."""
        mapping = LogicMapping(name=self.scope)
        mapping.frozen = True
        with self.assertRaises(RuntimeError):
            mapping['later'] = 1
        self.assertNotIn('later', mapping)
        self.assertEqual(len(mapping), 0)

    def test_03_what_the_mapping_holds_still_works(self) -> None:
        """The entries already there are read and written as before."""
        mapping = LogicMapping(name=self.scope)
        mapping['close'] = 5.0
        mapping.frozen = True

        with mapping:
            read = mapping['close']
        self.assertEqual(read.value, 5.0)
        mapping['close'] = 6.0
        self.assertEqual(read.value, 6.0)

    def test_04_a_read_of_a_held_entry_is_answered(self) -> None:
        """Reserving an entry that exists is not creating one."""
        mapping = LogicMapping(name=self.scope)
        mapping['close'] = 5.0
        mapping.frozen = True
        with mapping:
            self.assertEqual(mapping['close'].value, 5.0)

    def test_05_thawing_gives_the_shape_back(self) -> None:
        """Unsealing is the other half of the switch."""
        mapping = LogicMapping(name=self.scope)
        mapping.frozen = True
        mapping.frozen = False
        mapping['later'] = 1
        self.assertIn('later', mapping)

    def test_06_a_reserved_entry_is_answered_after_the_freeze(self) -> None:
        """The order this guards: reserve, THEN freeze, THEN read the reserved slot.

        A freeze takes away the store's ability to grow - nothing about what its
        entries hold. An entry that was reserved before the freeze and is written
        after it is a value like any other, and the read built over it in between
        answers as soon as there is something to answer with.
        """
        mapping = LogicMapping(name=self.scope)

        with mapping:
            reserved = mapping['open']  # 1. the entry is reserved by naming it
        mapping.frozen = True  # 2. and the store is closed for new ones
        with mapping:
            later = mapping['open']  # 3. a read of the reserved slot, built while frozen

        # The read holds where the entry is, and no type of its own.
        self.assertTrue(later.out.is_null)
        self.assertIs(later.out.dtype, VarType.inferred)

        with self.assertRaises(RuntimeError) as caught:
            later.eval()  # 4. nothing has landed in the entry yet
        self.assertIn('UNBOUND', str(caught.exception))

        mapping['open'] = 1.25  # a frozen store takes no new ENTRIES; this one is not new

        self.assertEqual(later.eval(), 1.25)
        self.assertIs(later.out.ref_base, VarType.double)
        self.assertEqual(later.value, 1.25)
        self.assertEqual(reserved.eval(), 1.25)  # the read made before the freeze, too

        # And the freeze still means what it says: a key that is not held is not
        # reserved, and the store does not take one (`with` because a read is
        # built inside the store it reads).
        with mapping, self.assertRaises(KeyError):
            mapping['brand_new']


class TestAttrExpression(MappingTestCase):
    """Contract: the read of one entry in the mapping a build is inside.

    Expected behavior:
        - outside a build there is no store to read, and the read says so;
        - inside a group that is not a mapping there is no store either;
        - inside a mapping it is a variable node, naming the entry it reads;
        - it names itself the way the capi names the same read.
    """

    def test_00_a_read_outside_a_build_is_refused(self) -> None:
        """There is no active group to read from."""
        with self.assertRaises(RuntimeError):
            AttrExpression('close')

    def test_01_a_read_in_a_plain_group_is_refused(self) -> None:
        """A group that holds nothing is not a store."""
        group = LogicGroup(name=self.scope)
        with group:
            with self.assertRaises(TypeError):
                AttrExpression('close')

    def test_02_a_read_in_a_mapping_is_a_variable_node(self) -> None:
        """Reading a store is what a variable node is for."""
        mapping = LogicMapping(name=self.scope)
        with mapping:
            read = mapping['close']
        self.assertIsInstance(read, VariableNode)
        self.assertEqual(read.type, 'VARIABLE')
        self.assertEqual(read.key, 'close')

    def test_03_a_read_names_itself_the_way_the_capi_does(self) -> None:
        """The display text is the attribute path: ``group.entry``."""
        mapping = LogicMapping(name=self.scope)
        with mapping:
            read = mapping['close']
        self.assertEqual(read.repr, f'{self.scope}.close')

    def test_04_a_read_is_registered_like_any_node(self) -> None:
        """It is a node of the graph, so the layer can find it by address."""
        mapping = LogicMapping(name=self.scope)
        with mapping:
            read = mapping['close']
        self.assertIn(read.address, NODE_REGISTRY)

    def test_05_a_read_composes_into_an_expression(self) -> None:
        """A store read is an operand like any other node."""
        mapping = LogicMapping(name=self.scope)
        with mapping:
            spread = mapping['close'] - mapping['open']
        self.assertIsInstance(spread, BinaryExpression)
        self.assertEqual(spread.op, ExpressionOperator.sub)

    def test_06_a_read_composes_with_a_literal(self) -> None:
        """And with a value that is not in the store at all."""
        mapping = LogicMapping(name=self.scope)
        with mapping:
            shifted = mapping['close'] + ConstantNode(1)
        self.assertEqual(shifted.op, ExpressionOperator.add)


class TestCapiParity(MappingTestCase):
    """Contract: the bake read is the capi read, over a different store.

    Expected behavior:
        - the same store and the same entry produce the same display text;
        - reading through the attribute gives the value the capi evaluates.

    The two layers build the same graph against different backends - one runs
    Python, one bakes C - so what a caller writes has to mean the same thing in
    both. Where they differ is deliberate: bake RESERVES the entry, so a read of
    one that is not held is an empty read rather than a KeyError.
    """

    def test_00_the_display_texts_agree(self) -> None:
        """Both name an entry ``group.entry``."""
        from decision_graph.decision_tree.capi.c_collection import LogicMapping as CapiMapping
        from decision_graph.decision_tree.capi.c_node import AttrExpression as CapiAttr

        bake_mapping = LogicMapping(name=self.scope)
        capi_mapping = CapiMapping(name=self.scope, data={})

        with bake_mapping:
            bake_read = bake_mapping['close']
        capi_read = CapiAttr(attr='close', logic_group=capi_mapping)

        self.assertEqual(bake_read.repr, capi_read.repr)

    def test_01_the_values_read_agree(self) -> None:
        """The same entry reads the same value in both."""
        from decision_graph.decision_tree.capi.c_collection import LogicMapping as CapiMapping
        from decision_graph.decision_tree.capi.c_node import AttrExpression as CapiAttr

        bake_mapping = LogicMapping(name=self.scope)
        bake_mapping['close'] = 12.5
        capi_mapping = CapiMapping(name=self.scope, data={'close': 12.5})

        with bake_mapping:
            bake_value = bake_mapping['close'].value
        capi_value = CapiAttr(attr='close', logic_group=capi_mapping).eval()

        self.assertEqual(bake_value, capi_value)

    def test_02_the_capi_refuses_an_entry_that_is_not_there(self) -> None:
        """The difference is the reservation, and it is the bake layer's alone."""
        from decision_graph.decision_tree.capi.c_collection import LogicMapping as CapiMapping
        from decision_graph.decision_tree.capi.c_node import AttrExpression as CapiAttr

        capi_mapping = CapiMapping(name=self.scope, data={})
        with self.assertRaises(KeyError):
            CapiAttr(attr='close', logic_group=capi_mapping).eval()

        bake_mapping = LogicMapping(name=self.scope)
        with bake_mapping:
            self.assertIsNone(bake_mapping['close'].value)  # reserved, and empty


class TestTheStoreAsAContainer(MappingTestCase):
    """Contract: the store answers what the capi's mapping answers, and one more.

    Expected behavior:
        - an empty store is false and a store with an entry is true, counted the
          way ``len`` counts - a reserved entry is an entry;
        - ``update`` writes several entries at once, in either of the two forms
          ``dict.update`` takes, and refuses a second positional argument as it
          does;
        - ``clear`` empties the store: nothing is held afterwards, and the store
          a build is inside is still the store it was.

    The capi's ``LogicMapping`` is the oracle for the surface - it has all three -
    and this layer's own reads are the oracle for what clearing must NOT do. A
    read built before the clear keeps a valid address: what it reports afterwards
    is an unfilled entry, never rubbish, and it follows the entry again once a
    value is written back.
    """

    def test_00_an_empty_store_is_false(self) -> None:
        """Nothing held, nothing to be true about."""
        mapping = LogicMapping(name=self.scope)
        self.assertFalse(mapping)
        self.assertEqual(len(mapping), 0)

    def test_01_a_reserved_entry_is_an_entry(self) -> None:
        """Reading is how entries come to exist, so a read is what makes it true."""
        mapping = LogicMapping(name=self.scope)
        with mapping:
            mapping['close']
        self.assertTrue(mapping)
        self.assertEqual(len(mapping), 1)

    def test_02_update_writes_from_a_mapping(self) -> None:
        """A mapping of names to values, written in one call."""
        mapping = LogicMapping(name=self.scope)
        mapping.update({'close': 1.5, 'open': 2.5})
        with mapping:
            self.assertEqual(mapping['close'].value, 1.5)
            self.assertEqual(mapping['open'].value, 2.5)

    def test_03_update_writes_from_pairs_and_keywords(self) -> None:
        """Both of ``dict.update``'s forms, and the keyword form beside them."""
        mapping = LogicMapping(name=self.scope)
        mapping.update([('close', 1.5), ('open', 2.5)], high=3.5)
        with mapping:
            self.assertEqual(mapping['high'].value, 3.5)
        self.assertEqual(len(mapping), 3)

    def test_04_update_reserves_an_entry_it_does_not_have(self) -> None:
        """Writing follows the same rule reading does: a new name is a new entry."""
        mapping = LogicMapping(name=self.scope)
        mapping.update(close=1.5)
        self.assertIn('close', mapping)

    def test_05_update_refuses_a_second_positional_argument(self) -> None:
        """One source of entries per call, as ``dict.update`` takes one."""
        mapping = LogicMapping(name=self.scope)
        with self.assertRaises(TypeError):
            mapping.update({'close': 1.5}, {'open': 2.5})

    def test_06_clear_empties_the_store(self) -> None:
        """Every entry goes: the count, the membership and the truth with them."""
        mapping = LogicMapping(name=self.scope)
        mapping.update(close=1.5, open=2.5)
        self.assertEqual(len(mapping), 2)

        mapping.clear()
        self.assertEqual(len(mapping), 0)
        self.assertFalse(mapping)
        self.assertNotIn('close', mapping)

    def test_07_a_read_outlives_a_clear_and_follows_a_refill(self) -> None:
        """The entry is dropped; the address the read names is not."""
        mapping = LogicMapping(name=self.scope)
        with mapping:
            read = mapping['close']
        mapping['close'] = 1.5
        self.assertEqual(read.value, 1.5)

        mapping.clear()
        self.assertIsNone(read.value)          # unfilled, not rubbish

        mapping['close'] = 9.5
        self.assertEqual(read.value, 9.5)      # the same entry, filled again

    def test_08_clear_empties_a_frozen_store_but_does_not_open_it(self) -> None:
        """Freezing seals the SHAPE; clearing is about the contents."""
        mapping = LogicMapping(name=self.scope)
        mapping.update(close=1.5)
        mapping.frozen = True

        mapping.clear()
        self.assertEqual(len(mapping), 0)
        with mapping:
            with self.assertRaises(KeyError):
                mapping['open']                # still sealed against new entries
