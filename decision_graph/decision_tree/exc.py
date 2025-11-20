__all__ = ['NodeError', 'TooManyChildren', 'TooFewChildren', 'NodeNotFountError', 'NodeValueError', 'EdgeValueError', 'ResolutionError', 'ExpressFalse', 'ContextsNotFound']

NO_DEFAULT = object()


class EmptyBlock(Exception):
    pass


class BreakBlock(Exception):
    pass


class NodeError(Exception):
    pass


class TooManyChildren(NodeError):
    pass


class TooFewChildren(NodeError):
    pass


class NodeNotFountError(NodeError):
    pass


class NodeValueError(NodeError):
    pass


class NodeTypeError(NodeError):
    pass


class NodeContextError(NodeError):
    pass


class EdgeValueError(NodeError):
    pass


class ResolutionError(NodeError):
    pass


class ExpressFalse(Exception):
    """Custom exception raised when a LogicExpression evaluates to False."""
    pass


class ContextsNotFound(Exception):
    pass
