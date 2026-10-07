"""Wrapper tests for the value layer's Python face, ``c_var.VarView``.

Scope is the view: what a ``dcg_var_t`` looks like from Python. The values
themselves are settled by ``tests/bake/test_c_var.c`` - what is checked here is
that a view taken over a live slot reports the tag, the payload and the absence
exactly as the C readers do.

A slot is not easy to come by from Python, which is what the view is for: nodes
own one (``LogicNode.out``), so the fixtures here are nodes - a literal for a
value, an unbound variable for a slot that holds nothing, a bound variable for a
slot that refers to another.

Oracle: the node the slot belongs to. Its own ``type`` and ``value`` say what the
view must report, so the view is checked against the wrapper that owns the slot
rather than against a re-derivation of its own arithmetic.
"""

import unittest

from decision_graph.decision_tree.bake.c_const import ConstantNode, VariableNode
from decision_graph.decision_tree.bake.c_var import VarNumeric, VarType, VarView

# Every literal a constant can be built from, with the tag name it becomes.
LITERALS = (
    (True, 'bool', True),
    (False, 'bool', False),
    (7, 'int', 7),
    (-7, 'int', -7),
    (2.5, 'double', 2.5),
    ('text', 'string', 'text'),
)


class TestVarView(unittest.TestCase):
    """Contract: a view reports the slot it was taken over, live and read-only.

    Expected behavior:
        - a literal's slot reads back as the value and the tag it went in as;
        - a slot that holds nothing is absent, and reads as None rather than as a
          value;
        - a slot that refers to another value reads through to it, and reports
          the reference it is;
        - a view keeps the value alive only through the node it came from.
    """

    def test_00_a_literal_slot_reads_back(self) -> None:
        """The tag, the payload and the Python value all agree with the node."""
        for value, tag, _ in LITERALS:
            with self.subTest(value=value):
                node = ConstantNode(value)
                view = node.out

                self.assertIsInstance(view, VarView)
                self.assertEqual(view.type_name, tag)
                self.assertFalse(view.is_null)
                self.assertFalse(view.is_ref)
                self.assertEqual(view.value, node.value)
                self.assertIs(type(view.value), type(node.value))

    def test_01_a_slot_that_holds_nothing_is_absent(self) -> None:
        """An unbound read is a slot with no value: None, and no error."""
        view = VariableNode().out

        self.assertTrue(view.is_null)
        self.assertIsNone(view.value)
        self.assertFalse(view.is_ref)

    def test_02_a_slot_that_refers_reads_through(self) -> None:
        """A bound read is a reference: it reports the hop and reads the target."""
        literal = ConstantNode(5)
        reader = VariableNode(key='n')
        reader.c_bind_const(literal)

        view = reader.out
        self.assertTrue(view.is_ref)
        self.assertEqual(view.ref_level, 1)
        self.assertEqual(view.value, 5)          # the same value the node reports
        self.assertEqual(view.value, reader.value)

        # And the target's own slot is the value, not a reference.
        self.assertFalse(literal.out.is_ref)

    def test_02_a_view_names_its_tag(self) -> None:
        """The tag is reported as a member of the value layer's own enum."""
        cases = (
            (ConstantNode(2), VarType.int, VarNumeric.int),
            (ConstantNode(2.5), VarType.double, VarNumeric.double),
            (ConstantNode(True), VarType.bool, VarNumeric.int),
            (ConstantNode('text'), VarType.string, VarNumeric.none),
        )
        for node, dtype, numeric in cases:
            with self.subTest(dtype=dtype.name):
                view = node.out
                self.assertIs(view.dtype, dtype)
                self.assertIs(view.numeric, numeric)
                self.assertEqual(int(view.dtype), dtype)

        # A reference reports the reference it is, and the base it walks to.
        variable = VariableNode(key='n')
        variable.c_bind_const(ConstantNode(7))
        self.assertIs(variable.out.dtype, VarType.int_ref)
        self.assertIs(variable.out.ref_base, VarType.int)
        self.assertIs(variable.out.numeric, VarNumeric.int)  # a reference reads as what it refers to

    def test_02_a_reference_names_both_ends_of_its_walk(self) -> None:
        """A reference reports the rung it is, and the tag at the end of it."""
        variable = VariableNode(key='n')
        variable.c_bind_const(ConstantNode(7))
        self.assertIs(variable.out.dtype, VarType.int_ref)
        self.assertIs(variable.out.ref_base, VarType.int)
        self.assertEqual(variable.out.ref_level, 1)
        self.assertEqual(variable.out.value, 7)  # read through, as every reader reads one

    def test_02_the_tag_algebra_is_a_level_and_a_base(self) -> None:
        """The two halves of a tag, as the C layer masks them: the members ARE the codes.

        Every reference tag is its base with the level in the high bits, so a
        reader gets the level from a mask and a shift and no table has to
        enumerate the pairs - which is what lets a new base tag arrive with its
        two rungs and nothing else to update.
        """
        self.assertEqual(int(VarType.double_ref), int(VarType.double) | 0x0100)
        self.assertEqual(int(VarType.double_ref_ref), int(VarType.double) | 0x0200)
        self.assertEqual(int(VarType.node_ref), int(VarType.node) | 0x0100)
        self.assertEqual(int(VarType.node_ref_ref), int(VarType.node) | 0x0200)
        self.assertEqual(int(VarType.inferred), int(VarType.reserved) | 0x0100)  # a reference to a slot with no type yet

    def test_03_the_numeric_readers_coerce(self) -> None:
        """The typed readers answer for the numeric tags, as the C ones do."""
        self.assertEqual(ConstantNode(7).out.as_int, 7)
        self.assertEqual(ConstantNode(7).out.as_double, 7.0)
        self.assertEqual(ConstantNode(2.5).out.as_double, 2.5)
        self.assertEqual(ConstantNode(2.5).out.as_int, 2)     # truncates toward zero
        self.assertIs(ConstantNode(True).out.as_bool, True)
        self.assertIs(ConstantNode(False).out.as_bool, False)

    def test_04_a_string_slot_reads_as_text(self) -> None:
        """The string reader is tag-faithful: text, or None for a string slot.

        A reader asked for a type the value does not carry is REFUSED, not
        answered - the layer reports the mistake where it is made rather than
        handing back a NULL that reads like a value - so a caller asks the tag
        first (``type_name``) or the absence (``is_null``), which is what the
        second assertion here does.
        """
        self.assertEqual(ConstantNode('text').out.as_string, 'text')

        empty = VariableNode().out
        self.assertTrue(empty.is_null)                        # nothing to read
        self.assertIsNone(empty.value)                        # and it reads as nothing

    def test_05_a_view_renders_the_value(self) -> None:
        """A view has display text of its own, and names itself by it."""
        view = ConstantNode(2.5).out
        self.assertEqual(view.format(), '2.5')
        self.assertEqual(repr(view), '<VarView(2.5)>')
        self.assertEqual(ConstantNode('text').out.format(), '"text"')
        self.assertEqual(ConstantNode(True).out.format(), 'true')

    def test_06_a_view_does_not_own_the_slot(self) -> None:
        """Dropping the view leaves the node and its value standing."""
        import gc

        node = ConstantNode(3)
        view = node.out
        del view
        gc.collect()

        self.assertEqual(node.value, 3)
        self.assertEqual(node.out.value, 3)   # a fresh view over the same slot

    def test_07_a_view_reads_the_slot_live(self) -> None:
        """A view is an address, not a snapshot: it follows what the slot does."""
        literal = ConstantNode(1)
        reader = VariableNode(key='n')
        reader.c_bind_const(literal)
        view = reader.out

        with self.assertRaises(PermissionError):
            literal.value = 9     # the debug setter writes, then refuses

        self.assertEqual(view.value, 9)
