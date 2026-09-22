"""The constant layer: the values a graph is given, and the reads of a store.

Two node types, and they are opposites. A constant CARRIES a value: what it
stands for is in the node, and a caller can let the Python object go the moment
it is built. A variable READS one: its value lives elsewhere - in a store's
entry, or in another node's slot it was bound to - and the node is the read of
it, so what it answers with is what that place holds at the moment it is read.

Both compose: an operand of an expression, a leaf of a branch.
"""

from typing import Any

from .c_expr import BinaryExpression, UnaryExpression
from .c_logic_group import LogicGroup
from .c_node import LogicNode


class ConstantNode(LogicNode):
    """A literal input: the value it stands for is in the node.

    The C type follows the value - a bool is a true or a false node, an int an
    int, a float a double, a str a string - so there is nothing for a caller to
    say that the value does not already say.
    """

    def __init__(self, value: Any, *, repr: str | None = None) -> None:
        """Build a literal for a Python value.

        Args:
            value: The literal: a bool, an int, a float or a str.
            repr: Display text to copy; the value's own text when not given.

        Raises:
            TypeError: When the value has no node type - a container, None.
            MemoryError: When the block cannot be allocated.
        """
        ...

    def __add__(self, other: LogicNode) -> BinaryExpression:
        """Compose a sum expression with another node."""
        ...

    def __sub__(self, other: LogicNode) -> BinaryExpression:
        """Compose a difference expression with another node."""
        ...

    def __mul__(self, other: LogicNode) -> BinaryExpression:
        """Compose a product expression with another node."""
        ...

    def __truediv__(self, other: LogicNode) -> BinaryExpression:
        """Compose a division expression with another node."""
        ...

    def __floordiv__(self, other: LogicNode) -> BinaryExpression:
        """Compose a floor-division expression with another node."""
        ...

    def __pow__(self, other: LogicNode) -> BinaryExpression:
        """Compose a power expression with another node."""
        ...

    def __neg__(self) -> UnaryExpression:
        """Compose a negation of this literal."""
        ...

    def __eq__(self, other: LogicNode) -> BinaryExpression:
        """Compose an equality CONDITION with another node.

        Comparing two nodes builds the comparison node, as it does in the capi -
        the result is a branch, not a bool.
        """
        ...

    def __ne__(self, other: LogicNode) -> BinaryExpression:
        """Compose an inequality condition with another node."""
        ...

    def __hash__(self) -> int:
        """Hash by the C block's address, so a node can key a dict."""
        ...

    def __lt__(self, other: LogicNode) -> BinaryExpression:
        """Compose a less-than condition with another node."""
        ...

    def __le__(self, other: LogicNode) -> BinaryExpression:
        """Compose a less-or-equal condition with another node."""
        ...

    def __gt__(self, other: LogicNode) -> BinaryExpression:
        """Compose a greater-than condition with another node."""
        ...

    def __ge__(self, other: LogicNode) -> BinaryExpression:
        """Compose a greater-or-equal condition with another node."""
        ...

    def __and__(self, other: LogicNode) -> BinaryExpression:
        """Compose a conjunction with another node."""
        ...

    def __or__(self, other: LogicNode) -> BinaryExpression:
        """Compose a disjunction with another node."""
        ...

    def __invert__(self) -> UnaryExpression:
        """Compose the negation of this literal as a condition."""
        ...

    @property
    def value(self) -> Any:
        """The value the literal holds, read by the value's own tag."""
        ...


class VariableNode(LogicNode):
    """A variable input: a node that reads a value rather than holding one.

    It holds no value of its own: what it reads is the slot it was bound to, and
    the read is live - whatever that slot holds when the node is read is what the
    node reports. A node built without a slot reflects nothing until one is
    given.
    """

    def __init__(self, *, key: str | None = None, repr: str | None = None) -> None:
        """Build a read of an entry.

        Args:
            key: Entry name in the group it is built inside, if any.
            repr: Display text to copy.

        Raises:
            MemoryError: When the block cannot be allocated.
        """
        ...

    def c_bind_const(self, node: ConstantNode) -> None:
        """Read a literal's slot, so the read follows the value it holds.

        Args:
            node: The literal whose value this read reflects.

        Raises:
            RuntimeError: When the C layer refuses the binding.
        """
        ...

    def __add__(self, other: LogicNode) -> BinaryExpression:
        """Compose a sum expression with another node."""
        ...

    def __sub__(self, other: LogicNode) -> BinaryExpression:
        """Compose a difference expression with another node."""
        ...

    def __mul__(self, other: LogicNode) -> BinaryExpression:
        """Compose a product expression with another node."""
        ...

    def __truediv__(self, other: LogicNode) -> BinaryExpression:
        """Compose a division expression with another node."""
        ...

    def __floordiv__(self, other: LogicNode) -> BinaryExpression:
        """Compose a floor-division expression with another node."""
        ...

    def __pow__(self, other: LogicNode) -> BinaryExpression:
        """Compose a power expression with another node."""
        ...

    def __neg__(self) -> UnaryExpression:
        """Compose a negation of this read."""
        ...

    def __eq__(self, other: LogicNode) -> BinaryExpression:
        """Compose an equality CONDITION with another node."""
        ...

    def __ne__(self, other: LogicNode) -> BinaryExpression:
        """Compose an inequality condition with another node."""
        ...

    def __hash__(self) -> int:
        """Hash by the C block's address, so a node can key a dict."""
        ...

    def __lt__(self, other: LogicNode) -> BinaryExpression:
        """Compose a less-than condition with another node."""
        ...

    def __le__(self, other: LogicNode) -> BinaryExpression:
        """Compose a less-or-equal condition with another node."""
        ...

    def __gt__(self, other: LogicNode) -> BinaryExpression:
        """Compose a greater-than condition with another node."""
        ...

    def __ge__(self, other: LogicNode) -> BinaryExpression:
        """Compose a greater-or-equal condition with another node."""
        ...

    def __and__(self, other: LogicNode) -> BinaryExpression:
        """Compose a conjunction with another node."""
        ...

    def __or__(self, other: LogicNode) -> BinaryExpression:
        """Compose a disjunction with another node."""
        ...

    def __invert__(self) -> UnaryExpression:
        """Compose the negation of this read as a condition."""
        ...

    @property
    def value(self) -> Any:
        """The value the read stands for, read through its binding."""
        ...

    @property
    def key(self) -> str | None:
        """The store entry this read names, or None when it names none."""
        ...

    @property
    def logic_group(self) -> LogicGroup | None:
        """The group this read belongs to, or None for a standalone node.

        The C field is an untyped pointer because the node layer cannot name a
        group without an upward edge (DEPENDENCY.md 4.2); what it holds is the
        group the read was built inside, which is a store's entry in the case
        that matters - see ``c_collections.AttrExpression``.
        """
        ...
