__all__ = [
    'NO_DEFAULT',
    'EmptyBlock', 'BreakBlock',
    'NodeError', 'TooManyChildren', 'TooFewChildren', 'NodeNotFountError', 'NodeValueError', 'NodeTypeError', 'NodeContextError',
    'EdgeValueError',
    'EvalFailureError',
    'BakeFailureError',
    'ResolutionError', 'ExpressFalse', 'ExpressEvaluationError', 'ContextsNotFound'
]

NO_DEFAULT = object()


class EmptyBlock(Exception):
    """Raised when a SkippableContextBlock is empty."""
    pass


class BreakBlock(Exception):
    """Base exception for skipping a SkippableContextBlock. Internal use only."""
    pass


class NodeError(Exception):
    """Base exception for node-related errors."""
    pass


class TooManyChildren(NodeError):
    """Raised when a node has too many children or when trying to add a child node exceeding its limits, and other exception situations related to or caused by too many child nodes."""
    pass


class TooFewChildren(NodeError):
    """Raised when a node has too few children."""
    pass


class NodeNotFountError(NodeError):
    """Raised when a specified node cannot be found."""
    pass


class NodeValueError(NodeError):
    """Raised when a node has an invalid value."""
    pass


class NodeTypeError(NodeError):
    """Raised when a node has an invalid type."""
    pass


class NodeContextError(NodeError):
    """Raised when errors occur in the LogicNode context manager protocol."""
    pass


class EdgeValueError(NodeError):
    """Raised when a NodeEdgeCondition has an invalid value."""
    pass


class EvalFailureError(RuntimeError, NodeError):
    """Raised when a node cannot produce a value.

    It is what an evaluation fails with, whichever door asked for it: one node's
    ``eval``, a dry run, or a walk from a root. It answers the three questions a
    caller has about a failure that a bare code cannot - WHICH node refused, at
    which of the three stages, and what was running when it did (the built-in rule
    for the node's type, the rule its type installed, or a hook somebody put
    there).

    A hook may raise it too, and with a ``code`` of its own: the node then ends
    with that code rather than with the generic ``DCG_ERR_HOOK``, which is how a
    Python hook says what went wrong the way a Cython hook says it by returning
    the code. A hook that raises anything else ends the node with ``DCG_ERR_HOOK``
    and has its own exception attached as ``__cause__`` - the failure is the
    hook's to describe, and nothing here replaces the description with a code.

    Attributes:
        code: The ``DCG_ERR_*`` code the node ended with.
        code_name: That code's stable name (``'MATH'``, ``'UNBOUND'``, ...).
        node: The wrapper of the node that refused, when the layer has one.
        node_type: The failing node's type name.
        node_repr: Its display text.
        address: The C block's address.
        source: What was running: ``'builtin'``, ``'type_rule'``, ``'hook'``,
            ``'pre_hook'``, ``'post_hook'``, or None.
        failed_at: The stage that failed - ``'pre'``, ``'eval'`` or ``'post'``.
        stages: The stages that completed, by name.
        run_id: The run the failure happened in; 0 when it was not a run.
    """

    def __init__(
        self,
        message: str,
        *,
        code: int = 0,
        code_name: str | None = None,
        node: object = None,
        node_type: str | None = None,
        node_repr: str | None = None,
        address: int | None = None,
        source: str | None = None,
        failed_at: str | None = None,
        stages: tuple[str, ...] = (),
        run_id: int = 0,
    ) -> None:
        """Initialize the failure.

        Args:
            message: What to say about it - the layer composes its own, naming the
                node, the code and the stages.
            code: The ``DCG_ERR_*`` code the node ended with. A hook raising this
                gives the code it wants the node to end with.
            code_name: That code's stable name.
            node: The wrapper of the node that refused.
            node_type: The failing node's type name.
            node_repr: Its display text.
            address: The C block's address.
            source: What was running when it failed.
            failed_at: The stage that failed.
            stages: The stages that completed.
            run_id: The run the failure happened in.
        """
        super().__init__(message)
        self.code = code
        self.code_name = code_name
        self.node = node
        self.node_type = node_type
        self.node_repr = node_repr
        self.address = address
        self.source = source
        self.failed_at = failed_at
        self.stages = stages
        self.run_id = run_id


class BakeFailureError(RuntimeError, NodeError):
    """Raised when a graph cannot be baked.

    A bake is the pass that makes a built graph ready to be walked: it verifies
    what an evaluation assumes, locks what it verified, and prepares the record a
    walk fills. What it refuses is a graph that is not walkable - an operand slot
    with no component behind it, an operator the node's arity does not apply, a
    node with no evaluation at all - and it refuses BEFORE anything is locked, so
    the graph a failed bake leaves behind is exactly the graph it was handed.

    The report travels with the failure rather than being replaced by the
    exception: it says which node refused, with which code, how many problems the
    pass found, and (since a failed bake changes nothing) that nothing was
    locked, sealed or prepared.

    Attributes:
        report: The ``BakeReport`` the pass filled.
    """

    def __init__(self, message: str, *, report: object = None) -> None:
        """Initialize the failure.

        Args:
            message: What to say about it - the layer composes its own, naming
                the node that refused and the code it ended with.
            report: The bake report, holding the code, the node that produced it,
                the problems found and what the pass affected.
        """
        super().__init__(message)
        self.report = report


class ResolutionError(NodeError):
    """Raised when an error occurs during resolution."""
    pass


class ExpressFalse(Exception):
    """Custom exception raised when a LogicExpression evaluates to False."""
    pass


class ExpressEvaluationError(Exception):
    """Raised when an error occurs during the evaluation of a LogicExpression."""
    pass


class ContextsNotFound(Exception):
    """Raised when required contexts of ``LogicGroup`` are not found."""
    pass
