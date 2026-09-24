"""The bake protocol: the pass that makes a built graph ready to be walked.

A build says what a graph IS and an evaluation says what it DOES. Between the two
sits this: the last thing done to a graph before it is walked, and the pass every
evaluation is written assuming has run. It is asked of a ROOT and of nothing else,
because a graph is entered from one place.

Three things, in the order they can be done in. The graph is VERIFIED - what an
evaluation assumes is established here and nowhere else, because the hot path
carries no check for a malformed node: every operand an operator takes has a
component behind it, every operator belongs to its node's arity and every operand
array is as long as that arity, and no node in the graph is one that has no
evaluation at all (a call). It is then LOCKED - every node the walk reached is
frozen, so the graph's structure is what it was, and every store the graph reads
is sealed, so no entry can appear in it afterwards. A frozen store cannot grow,
which is what keeps a resolved read's reference into its block good. What locking
does NOT stop is a value: a store is sealed in SHAPE, and a caller goes on
writing what it already holds - a graph is baked once and fed many times. It is
then PREPARED - the record a walk fills has its room made in advance, so an
evaluation allocates nothing.

A bake is all or nothing: a graph that fails the verification is not locked and
not prepared, so a caller that ignores the report is left with a graph that does
not work rather than one that half works. A bake that succeeds is idempotent -
asked again it locks nothing, seals nothing and prepares nothing.

The door is ``c_dcg_bake_root``, reached from ``RootLogicNode.bake``; what it
answers with is a ``BakeReport``.
"""

from .c_hierarchy import RootLogicNode
from .c_node import LogicNode


class BakeReport:
    """What a bake found, and what it affected.

    A wrapper over one ``dcg_bake_report`` block, which it either OWNS - a report
    of its own, the one a bake is asked to fill - or merely reads through, when a
    caller made the block itself. A report owns nothing behind its block: the node
    it may name belongs to the graph, so the block going is the whole of its
    release.

    Its fields read where the pass wrote them, so asking twice answers the same
    thing; and a report of its own that no bake has filled yet is a report of a
    bake that never ran - ``DCG_OK``, no node, no counts.

    Attributes:
        code: The ``DCG_ERR_*`` code the pass ended with - ``DCG_OK`` when the
            graph is baked.
        code_name: That code's stable name (``'OK'``, ``'TYPE'``, ``'UNBOUND'``).
        node: The wrapper of the node the first problem was found on, or None
            when the failure was the pass's own (a machine that ran out of room).
        errors: How many problems were found.
        nodes: How many nodes the pass walked - the branches, and the operands
            they read.
        depth: The deepest level the walk reached.
        locked: How many nodes this pass locked; zero on a re-bake, and zero for
            a validate-only bake.
        sealed: How many stores this pass sealed.
        capacity: The entries the root's record was prepared with; zero when the
            pass did not prepare one.
    """

    def __init__(self, address: int = 0, owner: bool = False) -> None:
        """A report of its own, or a window onto one that is somebody else's.

        Args:
            address: Address of a ``dcg_bake_report`` to read through, for the
                caller that made the block itself; 0, the default, makes a report
                of its own instead.
            owner: Whether THIS wrapper releases the block at that address. It is
                ignored when an address is not given - a report made here is the
                wrapper's own and is released with it.

        Raises:
            MemoryError: When a report of its own cannot be allocated.
        """
        ...

    @property
    def address(self) -> int:
        """The block's address - what every read here reads through."""
        ...

    @property
    def code(self) -> int:
        """The ``DCG_ERR_*`` code the pass ended with."""
        ...

    @property
    def code_name(self) -> str:
        """That code's stable name."""
        ...

    @property
    def node(self) -> LogicNode | None:
        """The node the first problem was found on, or None when it was the pass's own."""
        ...

    @property
    def errors(self) -> int:
        """How many problems the pass found."""
        ...

    @property
    def nodes(self) -> int:
        """How many nodes the pass walked."""
        ...

    @property
    def depth(self) -> int:
        """The deepest level the walk reached."""
        ...

    @property
    def locked(self) -> int:
        """How many nodes this pass locked."""
        ...

    @property
    def sealed(self) -> int:
        """How many stores this pass sealed."""
        ...

    @property
    def capacity(self) -> int:
        """The entries the root's record was prepared with."""
        ...


def c_dcg_bake_root(root: RootLogicNode, validate_only: bool = False) -> BakeReport:
    """Bake the graph a root stands at the top of, and report what it did.

    Args:
        root: The root of the graph. A root is the only entry, so this is the
            only door: a graph is entered from one place.
        validate_only: Whether to verify the graph and report without locking or
            preparing it - the question "would this bake?" asked without the
            commitment.

    Returns:
        The report: what the pass found, and what it affected.

    Raises:
        RuntimeError: When the root has no block (a wrapper that was never
            built).
        BakeFailureError: When the graph cannot be baked. Nothing was locked and
            nothing was prepared, and the failure carries the report.
    """
    ...
