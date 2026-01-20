import logging
import uuid
from collections.abc import Iterable, Callable, Generator
from typing import Any, Never, final

from decision_graph.decision_tree.exc import NO_DEFAULT

LOGGER: logging.Logger


class Singleton(object):
    """Lightweight base to mark extension types as singletons.

    Used internally by Cython classes to ensure only one instance of certain
    helper types is created per process. Users typically won't need this.
    """


# Edge condition types
class NodeEdgeCondition(Singleton):
    """Represents an edge condition in a decision graph.

    This is the base type for all concrete condition markers. Most users 
    don't create these directly; instead, use the pre-created constants
    like ``TRUE_CONDITION``, ``FALSE_CONDITION``, ``ELSE_CONDITION``, or
    let conditions be inferred automatically when building graphs.
    """

    @property
    def value(self) -> Any: ...


class ConditionElse(NodeEdgeCondition):
    """Represents an explicit "else" branch in decision trees.

    It matches when none of the other registered conditions match.
    """


class ConditionAny(NodeEdgeCondition):
    """Represents an unconditioned branch (always eligible as a fallback)."""


class ConditionAuto(NodeEdgeCondition):
    """Marker used internally to request auto-inference of the edge condition."""


class BinaryCondition(NodeEdgeCondition):
    """Base type for binary True/False conditions."""


class ConditionTrue(BinaryCondition):
    """The boolean True branch condition.

    Notes:
      - Truthy when converted to ``bool``.
      - ``int(ConditionTrue)`` equals ``1``.
      - Unary negation or bitwise invert toggles to ``FALSE_CONDITION``.
    """


class ConditionFalse(BinaryCondition):
    """The boolean False branch condition.

    Notes:
      - Falsy when converted to ``bool``.
      - ``int(ConditionFalse)`` equals ``0``.
      - Unary negation or bitwise invert toggles to ``TRUE_CONDITION``.
    """


# Pre-created condition singletons exposed by the module
NO_CONDITION: ConditionAny
ELSE_CONDITION: ConditionElse
AUTO_CONDITION: ConditionAuto
TRUE_CONDITION: ConditionTrue
FALSE_CONDITION: ConditionFalse


class SkipContextsBlock:
    """Context manager that may skip executing the body of a with-block.

    - If the entry check passes, ``__enter__`` returns ``self`` and normal
      execution proceeds until ``__exit__`` is called.
    - If the entry check fails, execution of the block is prevented via
      tracing hooks and an internal control-flow exception; ``__exit__`` then
      suppresses that exception so the program continues after the block.

    Attributes:
        default_entry_check (bool): If True, the block executes by default.
    """

    default_entry_check: bool

    @final
    def __enter__(self) -> SkipContextsBlock: ...

    @final
    def __exit__(
            self,
            exc_type: type[BaseException] | None,
            exc_value: BaseException | None,
            exc_traceback,
    ) -> bool | None: ...


class LogicExpression(SkipContextsBlock):
    """Represents a logical or mathematical expression with deferred eval.

    The expression can be a static value, an exception to raise on evaluation,
    or a callable returning a value. An optional ``dtype`` can be provided to
    enforce or check the evaluated type.

    This class supports boolean logic (``&``, ``|``, comparisons) and basic
    arithmetic operators that produce new ``LogicExpression`` instances.

    Attributes:
        expression (object): The underlying expression (value, exception, or callable).
        dtype (type | None): Optional type to enforce on evaluation, if requested.
        repr (str): String representation for debugging and logging.
        uid (uuid.UUID): Unique identifier for the LogicExpression instance.
    """

    expression: object
    dtype: type | None
    repr: str
    uid: uuid.UUID

    def __init__(
            self,
            *,
            expression: float | int | bool | Exception | Callable[[], Any] = None,
            dtype: type = None,
            repr: str = None,
            uid: uuid.UUID = None,
            **kwargs,
    ) -> None:
        """
        Initialize the LogicExpression.

        Args:
            expression (Union[Any, Callable[[], Any]]): A callable or static value.
            dtype (type, optional): The expected type of the evaluated value (e.g. float, int, or bool).
            repr (str, optional): A string representation of the expression.
            uid (uuid.UUID, optional): Unique identifier for the expression. If None, a new UUID is generated.
            kwargs: __cinit__ extra kwargs guardian of for subclassing support, not used is this base class.
        """

    def eval(self, enforce_dtype: bool = False) -> Any:
        """Evaluate the expression and return the resulting value.

        If ``enforce_dtype`` is True and a ``dtype`` was provided, the result
        is cast using ``self.dtype(value)``.
        """

    @classmethod
    def cast(
            cls,
            value: int | float | bool | Exception | LogicExpression | Callable[[], Any],
            dtype: type = None,
    ) -> LogicExpression:
        """
        Cast a value into a LogicExpression.
        If the value is already a LogicExpression, it is returned as-is (same instance).

        Returns:
            LogicExpression: The resulting LogicExpression instance.

        Raises:
            TypeError: If the value type is unsupported.
        """

    def __bool__(self) -> bool:
        """Evaluate the expression and return its boolean value."""

    def __and__(self, other: LogicExpression | bool) -> LogicExpression:
        """Return a new LogicExpression representing logical AND with ``other``."""

    def __eq__(self, other: object) -> LogicExpression:
        """Return a new LogicExpression representing equality comparison to ``other``."""

    def __or__(self, other: LogicExpression | bool) -> LogicExpression:
        """Return a new LogicExpression representing logical OR with ``other``."""

    def __add__(self, other: object) -> LogicExpression:
        """Return a new LogicExpression representing addition with ``other``."""

    def __sub__(self, other: object) -> LogicExpression:
        """Return a new LogicExpression representing subtraction with ``other``."""

    def __mul__(self, other: object) -> LogicExpression:
        """Return a new LogicExpression representing multiplication with ``other``."""

    def __truediv__(self, other: object) -> LogicExpression:
        """Return a new LogicExpression representing true division with ``other``."""

    def __floordiv__(self, other: object) -> LogicExpression:
        """Return a new LogicExpression representing floor division with ``other``."""

    def __pow__(self, other: object) -> LogicExpression:
        """Return a new LogicExpression representing exponentiation with ``other``."""

    def __lt__(self, other: object) -> LogicExpression:
        """Return a new LogicExpression representing less-than comparison to ``other``."""

    def __le__(self, other: object) -> LogicExpression:
        """Return a new LogicExpression representing less-than-or-equal comparison to ``other``."""

    def __gt__(self, other: object) -> LogicExpression:
        """Return a new LogicExpression representing greater-than comparison to ``other``."""

    def __ge__(self, other: object) -> LogicExpression:
        """Return a new LogicExpression representing greater-than-or-equal comparison to ``other``."""

    def __repr__(self) -> str:
        """Return the string representation of the LogicExpression."""


class LogicGroupManager(Singleton):
    """Singleton manager for LogicGroup instances and runtime expression context.

    Handles caching and reuse of ``LogicGroup`` objects and manages runtime
    stacks for active groups and nodes while building or evaluating decision
    graphs.

    Also supports shelving/unshelving state to create decision sub-graphs
    (for example, across function calls) without interfering with the main
    active state.

    Attributes:
        inspection_mode (bool): If True, generate layout without executing actions.
        vigilant_mode (bool): If True, perform stricter validation and avoid auto-generated nodes.
    """

    inspection_mode: bool
    vigilant_mode: bool

    def __call__(self, name: str, cls: type[LogicGroup], **kwargs) -> LogicGroup:
        """Get or create a cached LogicGroup instance with the given name.

        Useful for closed-loop operations that need to reuse the same logic group.

        Args:
            name (str): The name of the logic group.
            cls (type[LogicGroup]): The LogicGroup subclass to instantiate if not cached.
            **kwargs: Additional keyword arguments to pass to the LogicGroup constructor.

        Returns:
            LogicGroup: The cached or newly created LogicGroup instance.
        """

    def __contains__(self, instance: LogicGroup) -> bool:
        """Return True if the given LogicGroup instance is cached by this manager."""

    def shelve(self) -> None:
        """Shelve the current active group and node stacks and breakpoint stacks for later restoration.

        This is useful for creating isolated decision sub-graphs without interfering with the others graphs' state.

        With RootLogicNode contexts, the LGM automatically shelved and unshelved. See ``RootLogicNode`` for details.
        """

    def unshelve(self) -> None:
        """Restore the most recently shelved active group and node stacks."""

    def clear(self) -> None:
        """Clear all cached LogicGroup instances and reset runtime stacks."""

    @property
    def active_group(self) -> LogicGroup | None:
        """The currently active LogicGroup, or None if no group context is entered."""

    @property
    def active_node(self) -> LogicNode | None:
        """The currently active LogicNode expression, or None if no expression context is entered."""


# Global instance of the manager
LGM: LogicGroupManager


class LogicGroup:
    """A minimal context manager to scope logic groups and break operations.

    A logic group is a lightweight context that records its name and an
    optional parent, and it provides a ``Break`` exception type for orderly
    early exit via ``LogicGroup.break_``.

    In runtime mode, breaking from a logic group propagates through nested
    groups and moves the execution cursor to the first line after the block;
    during this process, on-exit hooks of nested groups and nodes run.

    In inspection mode, breaking does not raise immediately. Instead, missing
    branches are auto-filled with ``NoAction`` to allow layout evaluation to
    continue, especially when a break occurs before the other branch is built.

    Attributes:
        name (str): The name of the logic group.
        parent (LogicGroup | None): The parent logic group, if any.
        Break (type[BaseException]): The exception type used for breaks.
        contexts (dict[str, Any]): Context-specific storage for the group.
    """

    name: str
    parent: LogicGroup | None
    Break: type[BaseException]
    contexts: dict[str, Any]

    def __init__(self, *, name: str = None, parent: LogicGroup = None, contexts: dict = None, **kwargs):
        """Initialize a LogicGroup with the given name, parent, and contexts.

        Args:
            name (str): The name of the logic group. If None, a unique name is assigned.
            parent (LogicGroup | None): The parent logic group, if any.
            contexts (dict[str, Any] | None): Optional context-specific storage.
            kwargs: __cinit__ extra kwargs guardian of for subclassing support, not used is this base class.
        """

    def __repr__(self) -> str: ...

    def __enter__(self) -> LogicGroup:
        """Enter the logic group context and mark it as active."""
        ...

    def __exit__(
            self,
            exc_type: type[BaseException] | None,
            exc_value: BaseException | None,
            exc_traceback,
    ) -> bool | None:
        """Exit the logic group context, handling Break exceptions gracefully."""
        ...

    @classmethod
    def break_(cls, scope: LogicGroup = None) -> None:
        """Break out from the given ``scope`` (or the active scope if None).

        In inspection mode, the break is recorded to be connected to the next
        entered node. In runtime mode, this propagates break through nested
        groups until the target scope is exited.
        """
        ...

    def break_active(self) -> None:
        """Break out from the currently active logic group only (top of stack)."""

    def break_inspection(self) -> None:
        """Record a break while in inspection mode without raising immediately."""

    def break_runtime(self) -> None:
        """Propagate a break through nested groups until the target scope is exited."""


class LogicNode(LogicExpression):
    """A decision node that branches to children based on an evaluated value.

    Each child is registered against an edge condition, and the node supports
    auto-inference of binary conditions for succinct graph construction.

    Attributes:
        parent (LogicNode | None): The parent node, if any.
        condition_to_parent (NodeEdgeCondition): The edge condition leading to this node from its parent.
        children (dict[NodeEdgeCondition, LogicNode]): Mapping of edge conditions to child nodes.
        labels (list[str]): LogicGroup names this node belongs to.
        autogen (bool): Whether this node was auto-generated to fill a missing branch.
    """

    parent: LogicNode | None
    condition_to_parent: NodeEdgeCondition
    children: dict[NodeEdgeCondition, LogicNode]
    labels: list[str]
    autogen: bool

    def __init__(self, *, expression: object = None, dtype: type = None, repr: str = None, uid: uuid.UUID = None, **kwargs):
        """
        Initialize the LogicExpression.

        Args:
            expression (Union[Any, Callable[[], Any]]): A callable or static value.
            dtype (type, optional): The expected type of the evaluated value (e.g. float, int, or bool).
            repr (str, optional): A string representation of the expression.
            uid (uuid.UUID, optional): Unique identifier for the expression. If None, a new UUID is generated.
            kwargs: __cinit__ extra kwargs guardian of for subclassing support, not used is this base class.
        """

    def __rshift__(self, other: LogicNode) -> LogicNode:
        """
        Convenience for ``append`` to support chaining, e.g.::

        >>> node1 = LogicNode(expression=...)
        >>> node2 = LogicNode(expression=...)
        >>> node1 >> node2

        Returns the ``other`` node so calls can be chained.
        """

    def __call__(self, default: Any = NO_DEFAULT) -> Any:
        """Evaluate the tree from this node and return the final action/value.

        If ``default`` is not provided, a ``NoAction`` node will be used as the
        fallback terminal.

        You can pass ``NO_DEFAULT`` to explicitly require a matching branch;
        if no edge matches, a ``ValueError`` will be raised.
        See ``eval_recursively`` for details.
        """
        ...

    def append(self, child: LogicNode, condition: NodeEdgeCondition = AUTO_CONDITION) -> None:
        """Append a child node with the given edge condition.

        If ``condition`` is ``AUTO_CONDITION``, the condition is inferred
        automatically based on existing children (for binary branching).

        Raises:
            ValueError: If an invalid condition is provided (e.g., ``None``).
            KeyError: If the condition is already used by another child.
            EdgeValueError: If condition inference fails due to incompatible existing children.
            TooManyChildren: If inference fails due to too many existing children.
        """

    def overwrite(self, new_node: LogicNode, condition: NodeEdgeCondition) -> None:
        """Overwrite the child node for the given edge condition.

        If ``condition`` is ``AUTO_CONDITION``, the condition is inferred
        automatically based on existing children (for binary branching).

        Raises:
            ValueError: If an invalid condition is provided (e.g., ``None``).
            KeyError: If there is no existing child for the given condition.
            EdgeValueError: If condition inference fails due to incompatible existing children.
            TooManyChildren: If inference fails due to too many existing children.
        """

    def replace(self, original_node: LogicNode, new_node: LogicNode) -> None:
        """Replace an existing child node with a new node.

        Raises:
            RuntimeError: If the original node is currently active.
            LookupError: If the stack is out of sync with the node's children.
            NodeNotFountError: If the original node is not a child of this node.
        """

    def eval_recursively(
            self,
            path: list[LogicNode] = None,
            default: Any = NO_DEFAULT,
    ) -> tuple[Any, list[LogicNode]]:
        """Evaluate the decision tree recursively from this node.

        Args:
            path (list[LogicNode] | None): If provided, a list to record the
                sequence of nodes traversed during evaluation.
            default (Any): The default value or action to use if no matching
                child is found. Use ``NO_DEFAULT`` to request an error when no
                branch matches.
        Returns:
            tuple[Any, list[LogicNode]]: The resulting value/action and the
                path list of nodes traversed during evaluation.
        """

    def list_labels(self) -> dict[str, list[LogicNode]]:
        """List all LogicGroup names in the subtree rooted at this node.

        Returns:
            dict[str, list[LogicNode]]: A mapping from label strings (logic group names)
                to lists of nodes that have that label.
        """

    @property
    def leaves(self) -> Generator[LogicNode]:
        """An iterable of all leaf nodes in the subtree rooted at this node."""

    @property
    def is_leaf(self) -> bool:
        """True if this node has no children; otherwise False."""

    @property
    def child_stack(self) -> Iterable[LogicNode]:
        """An iterable of all child nodes in the subtree rooted at this node."""

    @property
    def descendants(self) -> Generator[LogicNode, None, None]:
        """A generator yielding all descendant nodes in the subtree rooted at this node."""
        ...


class BreakpointNode(LogicNode):
    """A logic node that represents a breakpoint in the decision tree, used for breaking out of logic groups.

    This node is auto-generated and can connect to at most one child node.
    To connect as branch to a BreakpointNode, there are 2 ways:
    - A managed style, powered by LGM, where the BreakpointNode connects to the next entered node, outside the LogicGroup this BreakpointNode from. after a break is recorded.
    - or a manual way, using with clause. Which entered the BreakpointNode context, and then continue to build the rest of the branch. In this way the LGM will relinquish the management of this node. This could be potentially dangerous, but provides a way to join external branches.

    During evaluation, if connected, it delegates to the child's evaluation; otherwise, it returns its default expression (NoAction) in vigilant mode.

    Attributes:
        break_from (LogicGroup): The logic group from which this breakpoint breaks.
        await_connection (bool): Whether to wait for a connection to a child node during inspection.
    """

    break_from: LogicGroup
    await_connection: bool

    def __init__(self, *, break_from: LogicGroup = None, expression: Any = None, repr: str = None, **kwargs):
        """
        Initialize the BreakpointNode.

        Args:
            break_from (LogicGroup): The logic group from which this breakpoint breaks. Defaults to None.
            expression (Any): The default expression for this breakpoint node. Defaults to a new unique dangling NoAction node.
            str (str): A string representation of the expression. Auto-generated if None.
        """

    @classmethod
    def break_(cls, break_from: LogicGroup, **kwargs) -> BreakpointNode:
        """
        Create a BreakpointNode that breaks from the given logic group, for full manual control.
        This method also auto connects the BreakpointNode to the active node in the LGM upon creation.

        Args:
            break_from (LogicGroup): The logic group from which this breakpoint breaks.
            kwargs: Additional keyword arguments to pass to the BreakpointNode constructor.
        """

    def connect(self, child: LogicNode) -> None:
        """
        Connect a child node to this BreakpointNode.

        Raises:
            TooManyChildren: If a child is already connected.
        """

    @property
    def linked_to(self) -> LogicNode:
        """
        The child node connected to this BreakpointNode.

        Returns:
            LogicNode: The connected child node, if there is one, otherwise return None.
        """


class ActionNode(LogicNode):
    """A terminal node that can execute an optional ``action`` upon selection."""

    action: Callable[[], Any] | None

    def __init__(
            self,
            *,
            action: Callable[[], Any] = None,
            expression: object = None,
            dtype: type = None,
            repr: str = None,
            auto_connect: bool = True,
            **kwargs,
    ) -> None:
        """
        Initialize the ActionNode.

        Args:
            action (Callable[[], Any] | None): An optional callable to execute when this action is selected (post-eval).
            expression (Union[Any, Callable[[], Any]]): A callable or static value (on-eval).
            dtype (type, optional): The expected type of the evaluated value (e.g. float, int, or bool).
            repr (str, optional): A string representation of the expression.
            auto_connect (bool): If True, automatically connect this action node to the active node in the LGM upon creation.
            kwargs: __cinit__ extra kwargs guardian of for subclassing support.
        """

    def __enter__(self) -> Never:
        """
        ActionNode does not support the context manager protocol.

        Raises:
            NodeContextError: Using ``with ActionNode()`` is invalid.
        """

    def append(self, child: LogicNode, condition: NodeEdgeCondition = AUTO_CONDITION) -> Never:
        """
        Appending children to an ActionNode is not supported.

        Since ActionNodes are terminal, ``replace`` and ``overwrite`` are also
        not applicable and will fail naturally if attempted.

        Raises:
            TooManyChildren: Always raised to signal invalid operation.
        """


class PlaceholderNode(ActionNode):
    """An action node that serves as a placeholder in the decision tree.

    This node is auto-generated and during evaluation returns itself in vigilant mode, otherwise returns a NoAction instance.
    """


class NoAction(ActionNode):
    """An action node whose evaluation returns itself and performs no action.

    attributes:
        sig (int): The signature marker for the no-action. Defaults to ``0``. Internal c fields is a ``ssize_t``
    """

    sig: int

    def __init__(
            self,
            *,
            sig: int = 0,
            action: Callable[[], Any] = None,
            expression: object = None,
            dtype: type = None,
            repr: str = None,
            auto_connect: bool = True,
            autogen: bool = False,
            **kwargs,
    ) -> None:
        """
        Initialize a NoAction node.

        Args:
            sig (int): The signature marker for the no-action. Defaults to ``0``.
            action (Callable[[], Any] | None): An optional callable to execute when this action is selected (post-eval).
            expression (Union[Any, Callable[[], Any]]): A callable or static value (on-eval).
            dtype (type, optional): The expected type of the evaluated value (e.g. float, int, or bool).
            repr (str, optional): A string representation of the expression.
            auto_connect (bool): If True, automatically connect this action node to the active node in the LGM upon creation.
            autogen (bool): If True, marks this node as auto-generated.
            kwargs: __cinit__ extra kwargs guardian of for subclassing support.
        """

    def __int__(self): ...


class LongAction(ActionNode):
    """An action node variant carrying a positive ``sig`` marker.

    Attributes:
        sig (int): The signature marker for the long action. Defaults to ``1``.
    """

    sig: int

    def __init__(
            self,
            *,
            sig: int = 1,
            action: Callable[[], Any] = None,
            expression: object = None,
            dtype: type = None,
            repr: str = None,
            auto_connect: bool = True,
            **kwargs,
    ) -> None:
        """
        Initialize a LongAction node.

        Args:
            sig (int): The signature marker for the long action. Defaults to ``1``.
            action (Callable[[], Any] | None): An optional callable to execute when this action is selected (post-eval).
            expression (Union[Any, Callable[[], Any]]): A callable or static value (on-eval).
            dtype (type, optional): The expected type of the evaluated value (e.g. float, int, or bool).
            repr (str, optional): A string representation of the expression.
            auto_connect (bool): If True, automatically connect this action node to the active node in the LGM upon creation.
            kwargs: __cinit__ extra kwargs guardian of for subclassing support.
        """

    def __int__(self): ...


class ShortAction(ActionNode):
    """An action node variant carrying a negative ``sig`` marker.

    Attributes:
        sig (int): The signature marker for the long action. Defaults to ``-1``.
    """

    sig: int

    def __init__(
            self,
            *,
            sig: int = -1,
            action: Callable[[], Any] = None,
            expression: object = None,
            dtype: type = None,
            repr: str = None,
            auto_connect: bool = True,
            **kwargs,
    ) -> None:
        """
        Initialize a ShortAction node.

        Args:
            sig (int): The signature marker for the short action. Defaults to ``-1``.
            action (Callable[[], Any] | None): An optional callable to execute when this action is selected (post-eval).
            expression (Union[Any, Callable[[], Any]]): A callable or static value (on-eval).
            dtype (type, optional): The expected type of the evaluated value (e.g. float, int, or bool).
            repr (str, optional): A string representation of the expression.
            auto_connect (bool): If True, automatically connect this action node to the active node in the LGM upon creation.
            kwargs: __cinit__ extra kwargs guardian of for subclassing support.
        """

    def __int__(self): ...


class CancelAction(ActionNode):
    """An action node whose evaluation returns itself and performs cancel action."""

    sig: int

    def __init__(
            self,
            *,
            sig: int = 0,
            action: Callable[[], Any] = None,
            expression: object = None,
            dtype: type = None,
            repr: str = None,
            auto_connect: bool = True,
            **kwargs,
    ) -> None:
        """
        Initialize a CancelAction node.

        Args:
            sig (int): The signature marker for the cancel action. Defaults to ``0``.
            action (Callable[[], Any] | None): An optional callable to execute when this action is selected (post-eval).
            expression (Union[Any, Callable[[], Any]]): A callable or static value (on-eval).
            dtype (type, optional): The expected type of the evaluated value (e.g. float, int, or bool).
            repr (str, optional): A string representation of the expression.
            auto_connect (bool): If True, automatically connect this action node to the active node in the LGM upon creation.
            kwargs: __cinit__ extra kwargs guardian of for subclassing support.
        """

    def __int__(self): ...


class ClearAction(ActionNode):
    """An action node whose evaluation returns itself and performs fully unwind (clear position) action."""

    sig: int

    def __init__(
            self,
            *,
            sig: int = 0,
            action: Callable[[], Any] = None,
            expression: object = None,
            dtype: type = None,
            repr: str = None,
            auto_connect: bool = True,
            **kwargs,
    ) -> None:
        """
        Initialize a ClearAction node.

        Args:
            sig (int): The signature marker for the clear action. Defaults to ``0``.
            action (Callable[[], Any] | None): An optional callable to execute when this action is selected (post-eval).
            expression (Union[Any, Callable[[], Any]]): A callable or static value (on-eval).
            dtype (type, optional): The expected type of the evaluated value (e.g. float, int, or bool).
            repr (str, optional): A string representation of the expression.
            auto_connect (bool): If True, automatically connect this action node to the active node in the LGM upon creation.
            kwargs: __cinit__ extra kwargs guardian of for subclassing support.
        """

    def __int__(self): ...
