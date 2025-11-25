from collections.abc import Iterator, Sequence, Generator
from typing import Any, Optional

from .c_abc import LogicGroup
from .c_node import GetterExpression, AttrExpression


class LogicMapping(LogicGroup):
    """A mapping-like logic group that decouples stored data from the
    runtime context of a logic group.

    Instances behave like a read/write mapping container at the Python
    level; expressions created against a `LogicMapping` instance will
    consult the mapping stored in the group's contexts.

    Attributes:
        data: The underlying dict object used to store key/value pairs.
    """

    data: dict[str, Any]

    def __init__(self, *, name: str = None, data: dict[str, Any] = None, parent: Any | None = None, contexts: Optional[dict] = None) -> None:
        """Initialize the LogicMapping.

        Arguments:
            name: Logical name for this group.
            data: Optional initial dict to use. If not a dict, it will be converted to a dict. If ``None``, the dict
                will be taken from or created inside the group's ``contexts``
                under the key ``'data'``.
            parent: Optional parent logic group (kept for parity with runtime
                behaviour; type is intentionally elided here).
            contexts: Optional dict to use for the group's contexts. When not
                provided, a default contexts mapping will be used/created by
                the runtime manager.
        """

    def __bool__(self) -> bool:
        """Return True when the underlying mapping is non-empty.

        Returns:
            True if the mapping contains at least one key; False otherwise.
        """

    def __len__(self) -> int:
        """Return the number of items stored in the mapping.

        Returns:
            The number of key/value pairs in the underlying mapping.
        """

    def __getitem__(self, key: str) -> AttrExpression:
        """Return an expression representing the named key in this mapping.

        The return value is an expression object (implementation-specific)
        that, when evaluated, will fetch the value stored under ``key``.

        Args:
            key: The mapping key.

        Returns:
            An AttrExpression that references ``key`` inside this mapping.
        """

    def __getattr__(self, key: str) -> AttrExpression:
        """Alias for ``__getitem__`` that allows attribute-style access.

        Args:
            key: The attribute name to access.

        Returns:
            An AttrExpression that references ``key`` inside this mapping.
        """

    def __contains__(self, key: str) -> bool:
        """Return True if ``key`` exists in the mapping."""

    def update(self, *args: Any, **kwargs: Any) -> None:
        """Update the underlying mapping with the provided items.

        Accepts the same arguments as :meth:`dict.update`.
        """

    def clear(self) -> None:
        """Remove all items from the underlying mapping."""


class LogicSequence(LogicGroup):
    """A sequence-like logic group that exposes an ordered collection to
    logic expressions.

    This class mimics the behaviour of Python sequences at the API layer
    so that expressions can index into it (e.g. ``seq[0]``) and tools can
    iterate over it to produce expression entries.

    Attributes:
        data: The underlying list object used to store items.
    """

    data: list[Any]

    def __init__(self, *, name: str = None, data: list[Any] = None, parent: Any | None = None, contexts: Optional[dict] = None) -> None:
        """Initialize the LogicSequence.

        Args:
            name: Logical name for this group.
            data: Optional initial list to use. If not a list, it will be converted to a list. If ``None``, the list
                will be taken from or created inside the group's ``contexts``
                under the key ``'data'``.
            parent: Optional parent logic group (opaque in this stub).
            contexts: Optional contexts mapping used by the runtime manager.
        """

    def __iter__(self) -> Iterator[GetterExpression]:
        """Yields GetterExpression objects representing each element in the sequence.

        Yields:
            GetterExpression objects (index-based) that can evaluate to the
            underlying sequence items.
        """

    def __len__(self) -> int:
        """Return the length of the underlying sequence."""

    def __getitem__(self, index: int) -> GetterExpression:
        """Return an expression referencing the element at ``index``.

        Negative indices follow Python semantics if the underlying
        sequence supports them.

        Args:
            index: The index to access.

        Returns:
            A GetterExpression that, when evaluated, returns the item at
            the requested index.
        """

    def __contains__(self, item: Any) -> bool:
        """Return True if ``item`` is present in the underlying sequence."""

    def append(self, value: Any) -> None:
        """Append ``value`` to the underlying sequence."""

    def extend(self, iterable: Sequence[Any]) -> None:
        """Extend the underlying sequence by the items from ``iterable``."""

    def insert(self, index: int, value: Any) -> None:
        """Insert ``value`` at position ``index`` in the underlying sequence."""

    def remove(self, value: Any) -> None:
        """Remove the first occurrence of ``value`` from the underlying sequence."""

    def pop(self, index: int = -1) -> Any:
        """Remove and return item at ``index`` (default last).

        Args:
            index: Position of item to pop; default -1 (last item).

        Returns:
            The removed item.
        """

    def clear(self) -> None:
        """Remove all items from the underlying sequence."""

    def __bool__(self) -> bool:
        """Return True when the underlying sequence is non-empty."""


class LogicGenerator:
    """Wraps a generator/iterator to expose generator protocol operations
    via a logic-group object.

    This object is a thin wrapper around a Python iterator/generator.
    It exposes the standard generator protocol methods so callers can use
    ``next()``, ``send()``, ``throw()``, and ``close()`` directly on the
    logic generator instance.

    Attributes:
        data: The underlying generator/iterator object.
    """

    data: Generator

    def __init__(self, *, name: str = None, data: Generator = None, parent: Any | None = None, contexts: Optional[dict] = None) -> None:
        """Initialize the LogicGenerator.

        Args:
            name: Logical name for this group.
            data: Optional generator/iterator object. If ``None``, the implementation
                may attempt to retrieve a generator from the group's
                ``contexts`` mapping (behavior depends on the runtime).
        """

    def __iter__(self) -> LogicGenerator:
        """Return self to support iteration protocol.

        Returns:
            The generator wrapper itself; calling ``iter()`` on the
            instance returns the same object so ``for x in instance:``
            works as expected.
        """

    def __next__(self) -> Any:
        """Return the next value from the wrapped generator.

        Raises:
            StopIteration: When the wrapped generator is exhausted.
        """

    def send(self, value: Any) -> Any:
        """Send a value into the wrapped generator.

        Delegates to the underlying generator's :meth:`send` method. The
        original exceptions from the wrapped generator (such as
        :class:`AttributeError` when the underlying iterator does not
        implement ``send``) are allowed to propagate.

        Args:
            value: The value to send into the generator.

        Returns:
            The next yielded value from the generator.
        """

    def throw(self, typ: type[BaseException], val: Optional[BaseException] = None, tb: Optional[Any] = None) -> Any:
        """Raise an exception inside the wrapped generator.

        Delegates directly to the wrapped generator's :meth:`throw`.

        Args:
            typ: Exception class to raise inside the generator.
            val: Optional exception instance or value.
            tb: Optional traceback object.

        Returns:
            The next yielded value from the generator (if any).
        """

    def close(self) -> None:
        """Close the wrapped generator by delegating to its :meth:`close`.

        Any exceptions raised by the generator's close method are
        propagated unchanged.
        """
