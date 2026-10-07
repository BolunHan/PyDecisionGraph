"""The other-process half of the reconstruction tests.

A reconstruction is only interesting where the Python side is NOT: in the process
that built a graph the registry already holds every wrapper, so asking for one is
a lookup. What has to be proved is that a graph can be reached from a process that
did not build it.

This module is both halves of that:

  - imported by the test, for ``dump_tree`` - the structural dump both sides
    print, so a rebuilt graph and the built one can be compared line for line;
  - run in a FORKED child (``main``), given the address of a node the parent
    built. The child clears its own registries first - they came across the fork,
    and a lookup would answer instead of a reconstruction - reconstructs the
    node, and prints the dump.

**Why a forked child and not a fresh interpreter.** The bake layer allocates
through the allocator protocol, whose shared region is UNLINKED as soon as it is
mapped (``AP_SHM_UNLINK_ON_MAP``), and whose name carries the creator's pid. So
the graph an allocator built is shared with every process forked from it and
attachable by no other: a fresh interpreter maps a region of its own, and the
address would name nothing there. The child here is a separate process with its
own wrappers and no registries to lean on, which is what the test is about.

Usage:  python bake_reconstruct_probe.py <address> <node|root>
"""

import sys

from decision_graph.decision_tree.bake.c_expr import ExpressionNode


def dump_tree(node, path: str = 'root') -> list:
    """Every node of a graph as text: what it is, where it hangs, what it holds.

    Walks the CHILD edges - the graph's shape - and reports how many operands
    every expression holds. It names each node by its C TYPE and its display text
    rather than by its Python class, because the class is a thing a build can be
    behind on: an arm the C layer grows is a placeholder converted in place, and
    the wrapper the layer holds for it is still the class the node WAS (the layer
    has no event for that yet). The classes a reconstruction gives are checked
    against a table in the test itself, where the graph is in hand.

    Nothing here is an address: two processes hold a graph at different
    addresses, and a rebuilt expression holds nodes of its own making.
    """
    lines = [
        f'{path} | {node.type} | {node.repr}'
        f' | parent={"yes" if node.parent is not None else "no"}'
        f' | cond={type(node.condition_to_parent).__name__}'
    ]
    if isinstance(node, ExpressionNode):
        lines.append(f'{path}.n_args | {node.n_args}')

    # Sorted by the EDGE's class, because the order the wrapper holds is the order
    # it was FILLED in, not the order the graph has: a build replaces a reserved
    # arm with a child, and the replace re-inserts the arm at the end of the dict.
    # A rebuilt wrapper can only follow the C child list, so the two orders differ
    # for every arm that was filled rather than reserved into place. What IS the
    # same in both processes is the layout - and that is the C renderer's, checked
    # where it lives (tests/test_bake_tree_build.py, arm order included).
    children = sorted(node.children.items(), key=lambda item: type(item[0]).__name__)
    for condition, child in children:
        lines.extend(dump_tree(child, f'{path}[{type(condition).__name__}]'))
    return lines


# The wrappers a cleared registry was holding. They are kept at module scope,
# not handed to the caller: a wrapper a build made OWNS its block, so the graph
# is freed the moment its last reference goes - and in a forked child that graph
# is the one the parent is still using. A caller who dropped this list by
# accident would be deleting the test's subject.
_PARKED = []


def clear_registries() -> list:
    """Make this process forget every wrapper it holds: the graph stays in C.

    A forked child starts with the builder's registries, which would answer every
    lookup with the builder's own wrappers - and a reconstruction would then have
    proved nothing. Emptying them leaves the C graph where it is and nothing on
    the Python side of it, which is the state a reconstruction is for.

    The wrappers are PARKED here rather than dropped (see ``_PARKED``), and the
    list comes back as well so a caller can look at what it was holding.
    """
    from decision_graph.decision_tree.bake.c_logic_group import GROUP_REGISTRY
    from decision_graph.decision_tree.bake.c_node import NODE_REGISTRY

    _PARKED.extend(NODE_REGISTRY.values())
    _PARKED.extend(GROUP_REGISTRY.values())
    NODE_REGISTRY.clear()
    GROUP_REGISTRY.clear()
    return list(_PARKED)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2

    from decision_graph.decision_tree.bake.c_reconstruct import (
        c_dcg_node_reconstruct_from_address,
        c_dcg_node_root_from_address,
    )

    clear_registries()

    address = int(sys.argv[1], 0)
    node = c_dcg_node_root_from_address(address) if sys.argv[2] == 'root' else c_dcg_node_reconstruct_from_address(address)

    for line in dump_tree(node):
        print(line)
    return 0


if __name__ == '__main__':
    sys.exit(main())
