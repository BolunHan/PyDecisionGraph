"""The collection layer: the store a build reads its inputs from.

A mapping is a logic group whose entries are keyed by name - the store a build
runs inside. What is wrapped here is the store, and the read of one entry: the
capi's ``LogicMapping`` and its ``AttrExpression``, with the read changed from a
Python expression evaluated later into a node of the graph, which is what a bake
graph needs.
"""

from typing import Any

from .c_const import VariableNode
from .c_logic_group import LogicGroup


class LogicMapping(LogicGroup):
    """A group that holds values, keyed by name.

    Reading is how entries come to exist: ``mapping[name]`` is a read, not a
    lookup - an entry the store does not hold yet is reserved, and the read
    points at the empty slot, so a graph can be written against a store that is
    filled in later. An entry's type is decided when a value lands in it, which
    is why a read of one is answered by the store rather than by the read's own
    binding.

    A store's shape is its own to close: ``frozen`` refuses a new entry from then
    on, and leaves what the store already holds - values, reads, writes - working.
    """

    def __init__(self, *, name: str | None = None, capacity: int = 0, parent: LogicGroup | None = None, **kwargs: Any) -> None:
        """Build a store.

        Args:
            name: The store's name - part of every read's display text, and
                refused when it is long enough to overflow that text.
            capacity: Initial room for entries; the layer's default when 0.
            parent: The group it is built inside, if any.

        Raises:
            ValueError: When the name is too long for the display buffer.
            MemoryError: When the block cannot be allocated.
        """
        ...

    def __getitem__(self, key: str) -> AttrExpression:
        """The read of an entry, reserving the entry when it is new.

        Args:
            key: Entry name.

        Returns:
            The read - a node of the graph, not the value.

        Raises:
            KeyError: When the store is frozen and the key is new.
        """
        ...

    def __getattr__(self, key: str) -> AttrExpression:
        """The same read, written as an attribute.

        Args:
            key: Entry name.

        Returns:
            The read of that entry.
        """
        ...

    def __setitem__(self, key: str, value: Any) -> None:
        """Put a value in an entry, reserving the entry when it is new.

        Args:
            key: Entry name.
            value: Value to store: a bool, int, float, str, or a node's value.

        Raises:
            RuntimeError: When the C layer refuses the write.
            KeyError: When the store is frozen and the key is new.
        """
        ...

    def __contains__(self, key: str) -> bool:
        """Whether an entry is held. Asking does not reserve one.

        Args:
            key: Entry name.
        """
        ...

    def __len__(self) -> int:
        """The entries held, reserved ones included."""
        ...

    @property
    def frozen(self) -> bool:
        """Whether the store is sealed against new entries.

        What the store already holds is unaffected: freezing seals its SHAPE,
        not its contents.
        """
        ...

    @frozen.setter
    def frozen(self, value: bool) -> None:
        """Seal or unseal the store."""
        ...


class AttrExpression(VariableNode):
    """The read of one entry in the store a build is inside.

    A variable node that names the group it reads, which is the whole of the
    difference between this and a bare variable: an entry is what it reads, and
    the store is what answers.

    A read begins holding WHERE its entry is - the entry's offset in the store,
    which no growth of the store can invalidate - and is resolved to the entry
    itself by its FIRST evaluation, which is where its type comes from. From then
    on the read holds that entry, and the values it reports are the entry's live
    ones for as long as they keep the type it was resolved to.
    """

    def __init__(self, name: str) -> None:
        """Build the read of an entry in the active store.

        Args:
            name: Entry name.

        Raises:
            RuntimeError: When no group is active.
            TypeError: When the active group is not a store.
            KeyError: When the store is sealed and the entry is new.
        """
        ...

    @property
    def value(self) -> Any:
        """The entry's value, read from the store of the moment.

        The value is read live either way: through the entry's offset while the
        read has not been evaluated, and through the entry itself once it has.
        None when the entry holds nothing yet.
        """
        ...

    @property
    def logic_group(self) -> LogicMapping:
        """The store this read names - never None for a read.

        The base declares the field untyped (the node layer cannot name a group
        without an upward edge, DEPENDENCY.md 4.2); a read is only ever built
        against a store, which is what makes this precise rather than optional.
        """
        ...
