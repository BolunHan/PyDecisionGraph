"""The hierarchy layer: the graph's entry point, and the inspection sink.

Two nodes that are not part of the decision the graph computes. A root is where
a graph is entered: it takes one branch, and entering it opens a context of its
own - the scopes around it are shelved while a build is inside. A breakpoint is
where an inspection stops: it breaks out of a group, and it resumes into the
next node entered outside it.
"""

from typing import Any

from .c_logic_group import LogicGroup
from .c_node import LogicNode


class RootLogicNode(LogicNode):
    """The graph's entry point: the only node that has no parent.

    A root takes exactly ONE branch - its single arm is the entry edge - and
    entering it shelves the contexts around it, so the graph a build enters is
    built in a context of its own.

    Attributes:
        name: The root's name, as the build gave it.
    """

    def __init__(self, *, name: str = 'Entry Point', inherit_contexts: bool = False, **kwargs: Any) -> None:
        """Build a root.

        Args:
            name: The root's name.
            inherit_contexts: Whether it takes the contexts of the groups around
                it rather than shelving them.

        Raises:
            MemoryError: When the block cannot be allocated.
        """
        ...

    @property
    def name(self) -> str | None:
        """The root's name - None for a root rebuilt from C, which carries none."""
        ...


class BreakpointNode(LogicNode):
    """An inspection sink: evaluation stops here and resumes outside the group.

    It borrows the group it breaks out of, and this wrapper is what holds that
    group alive while the breakpoint stands - which is why a reconstruction
    restores the group with it.
    """

    def __init__(self, *, break_from: LogicGroup | None = None, repr: str | None = None, autogen: bool = True, **kwargs: Any) -> None:
        """Build a breakpoint.

        Args:
            break_from: The group it breaks out of, if any.
            repr: Display text to copy.
            autogen: Whether the builder generated it rather than a caller.

        Raises:
            MemoryError: When the block cannot be allocated.
        """
        ...

    @property
    def break_from(self) -> LogicGroup | None:
        """The group this breakpoint breaks out of, or None when it names none."""
        ...

    @property
    def await_connection(self) -> bool:
        """Whether the breakpoint is waiting for the node it resumes into."""
        ...
