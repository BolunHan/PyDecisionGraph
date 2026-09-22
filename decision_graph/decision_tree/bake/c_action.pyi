"""The action layer: the leaves a graph decides with.

An action is where a graph stops and answers. It carries a signal - long, short,
flat - and is a typed node of its own, so a render, an evaluation and a rebuild
can all tell the leaves apart.

A leaf that is built inside a build joins the node being built, unless the caller
says otherwise; the action that joins nothing stands on its own.
"""

from typing import Any

from .c_node import LogicNode


class ActionNode(LogicNode):
    """An action leaf: a typed terminal node carrying a signal.

    The class itself is the family's own; a caller builds the named leaves below
    it, which differ only in the type and the signal they pass.
    """

    def __init__(self, node_type: int, *, repr: str | None = None, sig: int = 0, auto_connect: bool = True, **kwargs: Any) -> None:
        """Build an action of an explicit node type.

        Args:
            node_type: The C node type this action is.
            repr: Display text to copy; the type's own text when not given.
            sig: The signal it carries: +1 long, -1 short, 0 for the rest.
            auto_connect: Whether it joins the node being built.

        Raises:
            MemoryError: When the block cannot be allocated.
        """
        ...


class NoAction(ActionNode):
    """The leaf that decides nothing: what an unfilled arm closes as."""

    def __init__(self, *, sig: int = 0, repr: str = 'NoAction', auto_connect: bool = True, autogen: bool = False, **kwargs: Any) -> None:
        """Build a no-action leaf.

        Args:
            sig: The signal it carries, 0 by default.
            repr: Display text to copy.
            auto_connect: Whether it joins the node being built.
            autogen: Whether the builder generated it rather than a caller.
        """
        ...

    def __int__(self) -> int:
        """The signal the leaf carries."""
        ...


class LongAction(ActionNode):
    """The leaf that signals long."""

    def __init__(self, *, sig: int = 1, repr: str = 'LongAction', auto_connect: bool = True, **kwargs: Any) -> None:
        """Build a long-action leaf.

        Args:
            sig: The signal it carries, +1 by default.
            repr: Display text to copy.
            auto_connect: Whether it joins the node being built.
        """
        ...

    def __int__(self) -> int:
        """The signal the leaf carries."""
        ...


class ShortAction(ActionNode):
    """The leaf that signals short."""

    def __init__(self, *, sig: int = -1, repr: str = 'ShortAction', auto_connect: bool = True, **kwargs: Any) -> None:
        """Build a short-action leaf.

        Args:
            sig: The signal it carries, -1 by default.
            repr: Display text to copy.
            auto_connect: Whether it joins the node being built.
        """
        ...

    def __int__(self) -> int:
        """The signal the leaf carries."""
        ...


class CancelAction(ActionNode):
    """The leaf that cancels an outstanding signal."""

    def __init__(self, *, sig: int = 0, repr: str = 'CancelAction', auto_connect: bool = True, **kwargs: Any) -> None:
        """Build a cancel-action leaf.

        Args:
            sig: The signal it carries, 0 by default.
            repr: Display text to copy.
            auto_connect: Whether it joins the node being built.
        """
        ...

    def __int__(self) -> int:
        """The signal the leaf carries."""
        ...


class ClearAction(ActionNode):
    """The leaf that flattens: it clears whatever position is held."""

    def __init__(self, *, sig: int = 0, repr: str = 'ClearAction', auto_connect: bool = True, **kwargs: Any) -> None:
        """Build a clear-action leaf.

        Args:
            sig: The signal it carries, 0 by default.
            repr: Display text to copy.
            auto_connect: Whether it joins the node being built.
        """
        ...

    def __int__(self) -> int:
        """The signal the leaf carries."""
        ...
