__all__ = [
    'NO_DEFAULT',
    'EmptyBlock', 'BreakBlock',
    'NodeError', 'TooManyChildren', 'TooFewChildren', 'NodeNotFountError', 'NodeValueError', 'NodeTypeError', 'NodeContextError',
    'EdgeValueError',
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
