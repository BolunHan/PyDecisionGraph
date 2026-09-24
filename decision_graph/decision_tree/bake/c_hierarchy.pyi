"""The hierarchy layer: the graph's entry point, and the inspection sink.

Two nodes that are not part of the decision the graph computes. A root is where
a graph is entered: it takes one branch, and entering it opens a context of its
own - the scopes around it are shelved while a build is inside. A breakpoint is
where an inspection stops: it breaks out of a group, and it resumes into the
next node entered outside it.
"""

from collections.abc import Iterator
from typing import Any

from .c_bake import BakeReport
from .c_logic_group import LogicGroup
from .c_node import LogicNode


class RootLogicNode(LogicNode):
    """The graph's entry point: the only node that has no parent.

    A root takes exactly ONE branch - its single arm is the entry edge - and
    entering it shelves the contexts around it, so the graph a build enters is
    built in a context of its own.

    It is also where evaluating a graph starts: ``eval`` walks from here to a
    single leaf and keeps the record of how it got there, so the root is what a
    caller asks to make a decision.

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

    def eval(self) -> Any:
        """Evaluate the graph from this root, and record how it decided.

        The whole walk, not one node - which is what a root does differently
        from every other node, whose ``eval`` runs its own rule and nothing else.
        Every node on the way is evaluated, the record of the last walk is
        replaced, and the answer is where the walk came to rest: the leaf itself
        when that leaf is an action, because the action IS the decision, and the
        value in the leaf's slot otherwise.

        The value a walk reads is the value AT THE MOMENT OF THE WALK: a read is
        live, so the same graph decides differently when the store it reads
        changes, and evaluating again is how a caller asks it to. What a walk
        pins is the TYPE: a read is resolved to its entry's type at its first
        evaluation, and a value of another type written to that entry afterwards
        is not one it reports.

        Returns:
            The leaf's wrapper when the walk landed on an action, else the value
            that leaf holds.

        Raises:
            EvalFailureError: When the walk could not reach a leaf - a read of an
                entry with no value in it, a branch whose value selected no arm
                and which has no fallback, or a node a hook refused. It names the
                node that refused, with the code, the stage and what was running
                (``node`` is the node the record reports as ``failed``), and a
                hook's own exception travels as its cause. The record still holds
                the nodes reached before it stopped.
        """
        ...

    def bake(self, validate_only: bool = False) -> BakeReport:
        """Bake the graph: verify what an evaluation assumes, lock it, prepare it.

        The last thing done to a graph before it is walked, and the pass every
        evaluation is written assuming has run. What an evaluation assumes is
        established here and nowhere else, because the hot path carries no check
        for a malformed node:

        - the graph's STRUCTURE - one parentless root, the arms in reading order,
          an else last, an action below nothing, no cycles;
        - every operand an operator takes has a component bound to it, and its
          operand array is exactly as long as its arity - the rules run their
          operands and read the workspace they filled with nothing checked in
          between;
        - every operator belongs to its node's arity, and no node in the graph is
          one that has no evaluation at all (a call).

        What the pass then does is hold the graph to what it verified: every node
        it walked is frozen, so the structure is what it was, and every store the
        graph reads is sealed, so no entry can appear in it afterwards. That
        second half is the one an evaluation depends on - a resolved read holds a
        reference into its store's block, and a block moves only when the store
        grows. A store is sealed in SHAPE and not in contents: the entries it has
        are the entries it will have, and a caller goes on writing values into
        them, because a graph is baked once and fed many times.

        Finally the record a walk fills has its room made in advance, sized for
        what a walk from this root can need, so evaluating a baked graph
        allocates nothing.

        A bake is all or nothing: a graph that fails is not locked and not
        prepared, so nothing about it changes. A bake that succeeds is
        idempotent - asked again it locks nothing and seals nothing, which the
        report says.

        Args:
            validate_only: Whether to verify the graph and report without
                locking or preparing it - the question "would this bake?" asked
                without the commitment. Nothing about the graph changes.

        Returns:
            The report: what the pass found, and what it affected.

        Raises:
            BakeFailureError: When the graph cannot be baked - the code, the node
                that produced it and the problems found are in the report the
                failure carries, and nothing was locked or prepared.
        """
        ...

    @property
    def name(self) -> str | None:
        """The root's name - None for a root rebuilt from C, which carries none."""
        ...

    @property
    def eval_path(self) -> NodeEvalPathView:
        """The record of how the last decision was made, as a view over it.

        The root first and the leaf it came to rest on last, with every branch it
        descended through in between - the very wrappers the build made, not
        fresh views of them - plus the outcome: which code the walk ended with,
        where it landed, what failed if anything did, and which run wrote it. It
        is replaced by each walk, so what it holds is the last one; a walk that
        could not finish leaves the nodes it got through and no leaf.

        The view is taken once, when the root is built, and it reads the record
        the root holds - so a root rebuilt from C carries one too, over the very
        record the block has. It is a field rather than a computed value: asking
        for it twice answers with the same view.
        """
        ...


class NodeEvalPathView:
    """A read-only window onto ``dcg_node_eval_path``: the record of a walk.

    A view is an ADDRESS, not a copy: it holds the record it was taken over, and
    every read answers for what that record holds now. The record belongs to the
    root whose walk wrote it - it is embedded in that root and its blocks are
    nested under it - so the view must not outlive the root, which the layer
    guarantees by handing one out only from the root it belongs to
    (``RootLogicNode.eval_path``).

    It cannot write: the view holds the record **const**, and every reader it
    exposes reads through that pointer, so a write through a view does not
    compile.

    Attributes:
        _header: The C record this view reads through.
    """

    def __init__(self, address: int) -> None:
        """View the record at an address.

        Args:
            address: Address of a ``dcg_node_eval_path``. The layer hands these
                out itself (``RootLogicNode.eval_path``); nothing here checks that
                the address is a record, and the view keeps nothing alive.
        """
        ...

    def __repr__(self) -> str:
        """The view's own text: the count, the outcome and where the walk landed."""
        ...

    def __len__(self) -> int:
        """How many nodes the walk reached."""
        ...

    def __iter__(self) -> Iterator[LogicNode]:
        """Walk the entries: the nodes, in the order the walk reached them."""
        ...

    def __getitem__(self, index: int) -> LogicNode:
        """The entry at an index, counting from the front or the back.

        Args:
            index: Which entry; negative counts from the end.

        Returns:
            The wrapper for the node the walk reached there.

        Raises:
            IndexError: When there is no such entry.
        """
        ...

    @property
    def address(self) -> int:
        """The record's address - what this view reads through."""
        ...

    @property
    def nodes(self) -> list[LogicNode]:
        """Every entry, as a list: the nodes in the order the walk reached them."""
        ...

    @property
    def capacity(self) -> int:
        """The room the record has - how many entries it can hold before it grows."""
        ...

    @property
    def code(self) -> int:
        """The outcome, as the ``DCG_ERR_*`` code the walk ended with."""
        ...

    @property
    def code_name(self) -> str:
        """The outcome's stable name: ``'OK'``, ``'UNBOUND'``, ``'NO_MATCH'``, ..."""
        ...

    @property
    def leaf(self) -> LogicNode | None:
        """The node the walk came to rest on, or None when it never landed."""
        ...

    @property
    def failed(self) -> LogicNode | None:
        """The node that reported the failure, or None when nothing failed.

        In a walk this is the node whose own evaluation refused - the one whose
        stages say how far it got - which need not be the node a caller asked to
        evaluate: a root that reached a branch which then failed did not fail.
        """
        ...

    @property
    def seq_id(self) -> int:
        """The id of the run the record belongs to: which walk wrote it."""
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
