"""The logic group layer: the scopes a graph is built inside, and the manager.

A logic group is a scope, not a node: it carries a name, a type and a parent, and
a build entered into it is inside it. The manager is the build's state - which
group is active, which node is active, what has been entered and not yet left -
and it is what a `with` block talks to.

Groups register themselves with the manager under their name, and by their
address in the wrapper registry, so a group reached from C comes back as the
instance the build made.
"""

from typing import Any, Self

from cbase.bytemap import BoundByteMap

from .c_node import LogicNode


class LogicGroupNameRegistry(BoundByteMap):
    """The manager's name index: a group's name to the group itself."""

    def __getitem__(self, key: str) -> LogicGroup:
        """The group registered under a name.

        Args:
            key: The group's name.

        Returns:
            The group wrapper registered under it.

        Raises:
            KeyError: When no group is registered under that name.
        """
        ...


class LogicGroupManager:
    """The state of a build: the stacks of groups, nodes and breakpoints.

    It is the caller's struct, not an allocated block, and it keeps the
    allocator it was initialised with. A node entered with the `with` statement
    is placed by this manager - into the active node's reserved arm - and the
    manager knows the group a node belongs to.
    """

    def __init__(self) -> None:
        """Initialize an empty manager: no groups, no nodes, no breakpoints."""
        ...

    def register(self, group: LogicGroup) -> None:
        """Register a group under its name and its address.

        Args:
            group: Group to register - by name when it has one, always by
                address.
        """
        ...

    def find(self, name: str) -> LogicGroup | None:
        """The group registered under a name.

        Args:
            name: The group's name.

        Returns:
            The group, or None when no group is registered under that name.
        """
        ...

    def shelve(self) -> None:
        """Put the current build away, to be taken back by ``unshelve``."""
        ...

    def unshelve(self) -> None:
        """Take back a build that was shelved."""
        ...

    def clear(self) -> None:
        """Empty the manager: every stack, shelved state included."""
        ...

    def label_node(self, node: LogicNode) -> None:
        """Label a node with the names of the groups it was built inside.

        Args:
            node: Node to label.
        """
        ...

    def node_stack_append(self, node: LogicNode) -> None:
        """Push a node onto the manager's node stack.

        Args:
            node: Node being entered.
        """
        ...

    def node_stack_pop(self, node: LogicNode) -> None:
        """Pop a node off the manager's node stack.

        Args:
            node: Node being left.
        """
        ...

    @property
    def registry(self) -> LogicGroupNameRegistry:
        """The manager's own name index."""
        ...

    @property
    def inspection_mode(self) -> bool:
        """Whether a build records the path an evaluation takes."""
        ...

    @inspection_mode.setter
    def inspection_mode(self, value: bool) -> None:
        """Set the inspection flag."""
        ...

    @property
    def vigilant_mode(self) -> bool:
        """Whether the layer reports a mistake where it is made."""
        ...

    @vigilant_mode.setter
    def vigilant_mode(self, value: bool) -> None:
        """Set the vigilant flag."""
        ...

    @property
    def active_group(self) -> LogicGroup | None:
        """The group a build is currently inside, or None outside any group."""
        ...

    @property
    def active_node(self) -> LogicNode | None:
        """The node a build is currently inside, or None outside any node."""
        ...

    @property
    def address(self) -> int:
        """The manager's own address, which is how the layer names it to C."""
        ...

    @property
    def n_groups(self) -> int:
        """How many groups are entered."""
        ...

    @property
    def n_nodes(self) -> int:
        """How many nodes are entered."""
        ...

    @property
    def n_breakpoints(self) -> int:
        """How many breakpoints are queued and waiting."""
        ...


class LogicGroup:
    """A scope a graph is built inside.

    A group carries a name, a type and a parent, and a build entered into it is
    inside it - `with group:` enters, and leaving restores whatever was active
    before.
    """

    def __init__(self, *, name: str | None = None, parent: LogicGroup | None = None, **kwargs: Any) -> None:
        """Build a group of the base type.

        Args:
            name: The group's name.
            parent: The group it is built inside, if any.

        Raises:
            MemoryError: When the block cannot be allocated.
        """
        ...

    def __repr__(self) -> str:
        """The group's own text: its class and its name."""
        ...

    def __enter__(self) -> Self:
        """Enter the group as the scope of the build inside it.

        Returns:
            This group.
        """
        ...

    def __exit__(self, exc_type: Any, exc_value: Any, traceback: Any) -> bool:
        """Leave the group, restoring what was active before it.

        Returns:
            False, so an exception raised inside the block keeps travelling.
        """
        ...

    def break_(self, scope: LogicGroup | None = None) -> None:
        """Raise an inspection breakpoint inside this group.

        Args:
            scope: The group to break out of; this one when not given.
        """
        ...

    @classmethod
    def break_active(cls, scope: LogicGroup | None = None) -> None:
        """Raise an inspection breakpoint in the build that is running.

        Args:
            scope: The group to break out of.
        """
        ...

    @property
    def name(self) -> str | None:
        """The group's name, or None for a group that has none."""
        ...

    @property
    def parent(self) -> LogicGroup | None:
        """The group this one was built inside, or None."""
        ...

    @property
    def lgtype(self) -> int:
        """The group's C type, as the enumerator's value."""
        ...

    @property
    def address(self) -> int:
        """The group's C block, which is what the registry is keyed by."""
        ...


class LogicGroupWrapperRegistry(BoundByteMap):
    """The wrapper every group is looked up by, keyed by its block's address.

    The same rule the node registry keeps: a lookup that finds the address
    answers with the wrapper the layer holds, and one that misses builds a new
    wrapper over the block.
    """

    def __getitem__(self, key: int) -> LogicGroup:
        """The wrapper at an address, rebuilding it when it is not held.

        Args:
            key: Address of the C group block.

        Returns:
            The group wrapper for that address.
        """
        ...


# The manager every build runs through, and the registry every group is looked
# up by.
LGM: LogicGroupManager
GROUP_REGISTRY: LogicGroupWrapperRegistry
