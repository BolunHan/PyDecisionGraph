"""The way back: a node in C to the wrappers that stand for the graph it is in.

A graph is built from Python but lives in C, so a caller holding nothing but an
address - a process reading a graph out of memory another process shared, a
callback naming a node the C layer grew - needs the Python side back before
anything can be inspected or rendered.

A reconstruction restores the neighbourhood, not just the block: the node as the
class its type names, its children and their subtrees with the edges they hang
by, its parent and through it the whole graph up to the root, and what a family
carries beyond the edges - an expression's operands, a read's store, a
breakpoint's group.

Every wrapper the walk builds is registered, which is what makes one block one
Python object and what lets the walk terminate: a child asking for its parent
lands on the wrapper that is already there.

The variants themselves - one function per class, and the dispatch over them -
are ``cdef``, and not module attributes: this stub is the module's Python face.
"""

from .c_node import LogicNode


def c_dcg_node_reconstruct_from_address(address: int, owner: bool = False) -> LogicNode:
    """The wrapper for the node at an address, with its graph restored.

    Args:
        address: Address of the C node block. Zero is refused.
        owner: Whether the wrapper for THAT node may free its block; the
            wrappers the walk adds around it own nothing.

    Returns:
        The wrapper for the node, with its graph restored around it.

    Raises:
        ValueError: When the address is zero.
    """
    ...


def c_dcg_node_root_from_address(address: int, owner: bool = False) -> LogicNode:
    """The graph's entry point above a node: its root, reconstructed.

    A node reached in the middle of a graph is not the graph, and what a caller
    with only an address usually wants is the top of it. The root is found in C,
    by the parent chain, and reconstructed like any other node - which also means
    everything between it and the address comes back on the way.

    Args:
        address: Address of a node in the graph.
        owner: Whether the wrapper for the ROOT may free its block.

    Returns:
        The root of the graph the node is in.

    Raises:
        ValueError: When the address is zero.
    """
    ...
