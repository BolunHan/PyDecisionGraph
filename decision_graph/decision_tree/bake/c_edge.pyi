"""The edge layer: the condition a child carries about its parent.

An edge is a condition, and a condition is a tagged value with a type of its
own: five built-ins - unconditional, else, auto, true, false - and any other
condition is a caller's, holding the value its parent's evaluated result is
compared against.

Identity is the condition's ``type`` field, which is what the predicates here
read, and what the registry below is keyed by address for: a node's ``children``
is a dict keyed by these, so two wrappers of one condition have to compare as
one edge.
"""

from typing import Any

from cbase.bytemap import BoundByteMap


class NodeEdgeCondition:
    """A condition an edge carries, as a tagged value.

    An instance of this class is a **user condition**: it holds a value, and a
    parent takes the edge when its evaluated result matches that value. The five
    built-ins are instances of the subclasses below and are module constants.

    Attributes:
        _header: The C condition block the wrapper stands for.
        _owner: Whether this wrapper may free that block.
    """

    def __init__(self, py_value: Any = ..., repr: str | None = None) -> None:
        """Wrap a Python value as an edge condition.

        Args:
            py_value: The value the parent's result is compared against.
            repr: Display text for the condition.

        Raises:
            ValueError: When no value is given - a user condition needs one.
        """
        ...

    def __hash__(self) -> int:
        """Hash by the C block's address, or by the type for a built-in."""
        ...

    def __eq__(self, other: NodeEdgeCondition) -> bool:
        """Compare as edges: the tag first, then the payload.

        Args:
            other: Condition to compare against.

        Returns:
            True when both conditions are the same edge.
        """
        ...

    def __ne__(self, other: NodeEdgeCondition) -> bool:
        """The negation of ``__eq__``."""
        ...

    def __repr__(self) -> str:
        """The condition's display text, class name included."""
        ...

    def __str__(self) -> str:
        """The condition's display text."""
        ...

    @property
    def address(self) -> int:
        """The block this condition is, as the edge registry keys it."""
        ...

    @property
    def is_none(self) -> bool:
        """Whether this is the unconditional edge."""
        ...

    @property
    def is_else(self) -> bool:
        """Whether this is the fallback edge."""
        ...

    @property
    def is_auto(self) -> bool:
        """Whether this edge's arm is for the parent to infer."""
        ...

    @property
    def is_binary(self) -> bool:
        """Whether this is a two-way edge: the true or the false arm."""
        ...


class ConditionAny(NodeEdgeCondition):
    """The unconditional edge: a parent's single arm."""


class ConditionElse(NodeEdgeCondition):
    """The fallback edge: taken when nothing else matched, always last."""


class ConditionAuto(NodeEdgeCondition):
    """An unresolved edge: the parent infers the arm it belongs to."""


class BinaryCondition(NodeEdgeCondition):
    """A two-way edge: the true arm or the false arm of a branch."""


class ConditionTrue(BinaryCondition):
    """The true arm of a two-way branch."""

    def __bool__(self) -> bool:
        """True, as the arm it is."""
        ...

    def __int__(self) -> int:
        """1, as the arm it is."""
        ...


class ConditionFalse(BinaryCondition):
    """The false arm of a two-way branch."""

    def __bool__(self) -> bool:
        """False, as the arm it is."""
        ...

    def __int__(self) -> int:
        """0, as the arm it is."""
        ...


class EdgeConditionRegistry(BoundByteMap):
    """The wrapper every edge is looked up by, keyed by its block's address.

    A lookup that finds the address answers with the instance held for it; one
    that misses builds the condition its own **type** names - the derived class
    for a built-in, the base for a caller's - over the address that was asked
    for. Two conditions of one type are still two edges, so nothing here hands
    out a shared instance for a different address.
    """

    def __getitem__(self, key: int) -> NodeEdgeCondition:
        """The condition at an address, rebuilding it when it is not held.

        Args:
            key: Address of the C condition block.

        Returns:
            The condition wrapper for that address.
        """
        ...


# The five built-ins, and the registry that holds them.
NO_CONDITION: ConditionAny
ELSE_CONDITION: ConditionElse
AUTO_CONDITION: ConditionAuto
TRUE_CONDITION: ConditionTrue
FALSE_CONDITION: ConditionFalse
EDGE_REGISTRY: EdgeConditionRegistry
