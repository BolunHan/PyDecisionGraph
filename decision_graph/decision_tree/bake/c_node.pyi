"""The node layer: the graph's nodes, their edges, and the wrapper registry.

A node is a block in C and a wrapper here, and the two are held together by
address: ``NODE_REGISTRY`` is keyed by the block's address, so one node is one
Python object for as long as the layer holds it.

A node that is entered with the ``with`` statement is a scope a build descends
into - the manager reserves the arms its branches will fill - and the children
dict is the graph as Python sees it, keyed by the edge each child hangs by.
"""

from typing import Any, Self
from uuid import UUID

from cbase.bytemap.c_bytemap import BoundByteMap

from .c_edge import NodeEdgeCondition
from .c_logic_group import LogicGroupManager
from .c_var import VarView


class LogicNode:
    """A node of a bake graph: a block in C, wrapped here.

    Building is the ``with`` statement: entering a node reserves the arms its
    branches will be filled into, and whatever a build leaves reserved is closed
    by the C layer on the way out. A node reached from C - a child the C layer
    grew, a node named by an address - is turned back into a wrapper by
    ``c_reconstruct``, which is the only place a node's class is decided.
    """

    def __init__(self, *, repr: str | None = None, uid: Any | None = None, **kwargs: Any) -> None:
        """Initialize a node of no particular type, which is refused.

        Args:
            repr: Display text to copy.
            uid: Stable identity, minted when it is not given.

        Raises:
            NodeTypeError: Always - the base has no node type to build.
        """
        ...

    @staticmethod
    def get_manager() -> LogicGroupManager:
        """The manager this layer builds through.

        Returns:
            The process-wide logic group manager.
        """
        ...

    def __repr__(self) -> str:
        """The wrapper's own text: its class and its display text."""
        ...

    def __rshift__(self, other: LogicNode) -> LogicNode:
        """Link a child under the inherited edge, and answer with the child.

        Args:
            other: Node to link under this one.

        Returns:
            The child, so a shift chain reads as the graph it builds.
        """
        ...

    def __enter__(self) -> Self:
        """Enter the node as a scope a build continues inside.

        Returns:
            This node.
        """
        ...

    def __exit__(self, exc_type: Any, exc_value: Any, traceback: Any) -> bool:
        """Leave the node, closing whatever the build left reserved.

        Returns:
            False, so an exception raised inside the block keeps travelling.
        """
        ...

    def append(self, child: LogicNode, condition: NodeEdgeCondition | None = None) -> None:
        """Link a child under an explicit edge, or the inherited one.

        Args:
            child: Node to link.
            condition: Edge to link it by; the edge it reports when not given.

        Raises:
            RuntimeError: When the C layer refuses the link.
        """
        ...

    def overwrite(self, new_node: LogicNode, condition: NodeEdgeCondition) -> None:
        """Put a node into an edge another node already holds.

        Args:
            new_node: Node to place.
            condition: The edge to place it on.

        Raises:
            KeyError: When no child hangs by that edge.
        """
        ...

    def replace(self, original_node: LogicNode, new_node: LogicNode) -> None:
        """Put a node where another one is, inheriting its edge and position.

        Args:
            original_node: Node being displaced.
            new_node: Node taking its place.
        """
        ...

    def detach(self) -> None:
        """Unlink this node from its parent, keeping its own subtree."""
        ...

    def label(self, name: str) -> None:
        """Add a label to this node.

        Args:
            name: Label to add.
        """
        ...

    def unlabel(self, name: str) -> None:
        """Remove a label from this node.

        Args:
            name: Label to remove.
        """
        ...

    def has_label(self, name: str) -> bool:
        """Whether this node carries a label.

        Args:
            name: Label to look for.

        Returns:
            True when the label is on this node.
        """
        ...

    def render(self, max_depth: int = 0, show_labels: bool = True, show_out: bool = False, style: str = 'unicode') -> str:
        """Render the subtree as text.

        Args:
            max_depth: How deep to walk; 0 for the layer's own limit.
            show_labels: Whether to print each node's labels.
            show_out: Whether to print each node's value.
            style: ``'unicode'`` for box-drawing, anything else for ASCII.

        Returns:
            The tree, one node per line.

        Raises:
            BufferError: When the tree does not fit the render buffer.
        """
        ...

    def validate(self) -> tuple[int, int, int, int] | None:
        """Check the graph this node heads.

        Returns:
            None when the graph is well formed, else the report as
            ``(code, errors, nodes, depth)``.
        """
        ...

    @property
    def repr(self) -> str:
        """The node's display text."""
        ...

    @property
    def type(self) -> str:
        """The node's type name, as the C layer reports it."""
        ...

    @property
    def uuid(self) -> UUID:
        """The node's stable identity, minted when the node was built."""
        ...

    @property
    def autogen(self) -> bool:
        """Whether the builder generated this node rather than a caller."""
        ...

    @property
    def is_leaf(self) -> bool:
        """Whether the node has no children."""
        ...

    @property
    def labels(self) -> list[str]:
        """The labels on this node, in the order they were added."""
        ...

    @property
    def size(self) -> int:
        """How many nodes the subtree under this one holds, itself included."""
        ...

    @property
    def out(self) -> VarView:
        """A read-only view of the value this node holds.

        Every node has one output slot, and what is in it depends on the node: a
        literal holds its value there (which is what makes a constant a valid
        place to take a view from), an evaluated node holds what it last produced,
        and a node that has not run holds nothing. The view reads the slot live
        and keeps it alive only through this node.
        """
        ...

    @property
    def address(self) -> int:
        """The C block's address, which is what the registry is keyed by."""
        ...

    @property
    def parent(self) -> LogicNode | None:
        """The node this one hangs from, or None for a parentless node."""
        ...

    @property
    def children(self) -> dict[NodeEdgeCondition, LogicNode]:
        """The children, keyed by the edge each one hangs by."""
        ...

    @property
    def condition_to_parent(self) -> NodeEdgeCondition:
        """The edge this node hangs by - unconditional when it has no parent."""
        ...


class PlaceholderNode(LogicNode):
    """A reserved arm: where a branch goes if a build gives it one.

    A placeholder is the C layer's stand-in for a branch that has not been built
    yet. A branch that is built replaces it, and one that never is becomes an
    auto-generated no-action when the node around it is left.
    """

    def __init__(self, **kwargs: Any) -> None:
        """Allocate a stand-in.

        Raises:
            MemoryError: When the block cannot be allocated.
        """
        ...


class LogicNodeRegistry(BoundByteMap):
    """The wrapper every node is looked up by, keyed by its block's address.

    A lookup that finds the address answers with the wrapper the layer holds for
    it; one that misses hands the node to the reconstruction, which is what makes
    a node reached from C come back as the class its type names.
    """

    def __getitem__(self, key: int) -> LogicNode:
        """The wrapper at an address, rebuilding it when it is not held.

        Args:
            key: Address of the C node block.

        Returns:
            The wrapper for that address.
        """
        ...


# The registry every node is looked up by.
NODE_REGISTRY: LogicNodeRegistry
