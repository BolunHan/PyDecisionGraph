import enum
from collections.abc import Callable
from typing import Any, final, Generic, TypeVar

from .c_abc import LogicNode, LogicGroup, NodeEdgeCondition, BreakpointNode
from ..exc import NO_DEFAULT

UNARY_OP_FUNC = Callable[[Any], Any]
BINARY_OP_FUNC = Callable[[Any, Any], Any]
T = TypeVar('T')


class NodeEvalPath(list[LogicNode], Generic[T]):
    def to_clipboard(self) -> str:
        """Copy the evaluation path to the system clipboard as text.

        Generates a json representation of the evaluation path and copies it to the clipboard for easy sharing or logging.
        ``pyperclip`` module required.

        Returns:
            The generated text representation of the evaluation path.

        Raises:
            PyperclipException: If copying to clipboard fails.
        """


class RootLogicNode(LogicNode):
    """Entry point node for a decision graph.

    This node acts as the root container for a single child node and
    largely delegates evaluation and rendering to that child. It enforces
    that at most one child may be appended.

    Apart from a normal LogicNode, the RootLogicNode:
    - contains only 1 child node, with NO_CONDITION.
    - always returns True for `c_entry_check()`.
    - automatically shelve and unshelve contexts entering/exiting.
    - automatically triggers inspection mode on entering and restore to previous mode on exit.

    With these features, the RootLogicNode is designed as the Root node of a decision tree.

    Attributes:
        inherit_contexts: Whether to inherit outer logic groups when entered.
        eval_path: List of nodes evaluated during the last evaluation. In cython interface this is a reflected copy, In python interface this is the actual list.
    """
    inherit_contexts: bool
    eval_path: NodeEvalPath[LogicNode]

    def __init__(self, name: str = 'Entry Point', inherit_contexts: bool = False, **kwargs) -> None:
        """Create a RootLogicNode.

        The constructor automatically passes the kwargs to underlying base classes. If any kwargs are provided, it can mess up the normal initializing process. It is recommended to not provide any kwargs and leave as is.

        Example:

            >>> with RootLogicNode() as root:
            ...     with LogicNode() as child:
            ...         ...
            ...     # There should not be a second node appended to root

        Args:
            name: Optional name for the root node.
            **kwargs: Implementation-specific options.
        """

    @final
    def __enter__(self) -> RootLogicNode:
        """Enter context manager for the root node, to build a new decision graph in a seperated context.

        On entering:
        0. The RootLogicNode itself will be appended to active node stack.
        1. Then the LGM will be shelved to preserve outer contexts.
        2. With ``inherit_contexts`` flags, the outer LogicGroup contexts will be inherited.
        3. After shelfing, a fresh LGM will be provided, with only this RootLogicNode activated.

        Then on exiting, the LGM will be restored to previous state.

        The __enter__ method is not overridden actually, only the internal c hook function.

        Returns:
            The RootLogicNode instance.
        """

    def dry_run(self) -> None:
        """Perform a dry run evaluation of the decision tree without executing actions.

        This method traverses the decision tree starting from the root node,
        evaluating conditions and logging the evaluation path without
        executing any actions associated with the nodes.

        Raises:
            ExpressEvaluationError: If an error occurs during evaluation.
        """

    def get_breakpoint(self) -> BreakpointNode | None:
        """Get dangling breakpoint node attached to the root, if any.
        Returns:
            BreakpointNode if exists, else None.
        """

    def append(self, child: LogicNode, condition: NodeEdgeCondition = NO_DEFAULT) -> None:
        """Append a child node to the root.

        Args:
            child: The child logic node to append.
            condition: Edge condition associated with the child (ignored for
                the root; defaults to None).

        Raises:
            TooManyChildren: If a child is already attached to the root.
        """

    def to_html(self, file_name: str = 'root.html', with_eval: bool = True) -> None:
        """Render the decision tree to an HTML file.

        This method generates a standalone HTML file visualizing the
        decision tree structure starting from this root node. If
        ``with_eval`` is True, the evaluation results are also included
        in the rendering.

        Args:
            file_name: Output HTML file name.
            with_eval: Whether to include evaluation results in the rendering.
        """

    def show(self, **kwargs):
        """Render and display the decision tree in a interactive web page.
        This method generates an interactive visualization of the decision
        tree structure starting from this root node. Additional keyword
        arguments are passed to the underlying rendering flask engine.

        Arguments:
            **kwargs: keyword arguments passed into ``decision_graph.decision_tree.webui.show`` method.
        """

    def watch(self, **kwargs):
        """Continuously monitor and update the decision tree visualization in a web page.

        This method sets up a live monitoring session where the decision
        tree structure and evaluation results are periodically refreshed
        and displayed in an interactive web page.

        Arguments:
            **kwargs: keyword arguments passed into ``decision_graph.decision_tree.webui.watch`` method.
        """

    @property
    def child(self) -> LogicNode:
        """Return the single child node attached to the root.

        Raises:
            TooFewChildren: If no child is attached to the root.
        """
        ...


class ContextLogicExpression(LogicNode):
    """Base class for expressions that evaluate against a logic group/context.

    This class implements Python operator overloads so expressions can be
    composed using normal Python syntax (e.g. a + b, a > b, a & b).

    Attributes:
        logic_group: The LogicGroup used as the evaluation context.
    """

    logic_group: LogicGroup
    repr: str

    def __init__(self, *, logic_group: LogicGroup = None, **kwargs) -> None:
        """Create a context-aware expression.

        If ``logic_group`` is omitted the current active logic group is used.

        Args:
            logic_group: Optional logic group or mapping providing contexts.
            **kwargs: Implementation-specific keyword arguments.
        """

    # Mapping / attribute access
    def __getitem__(self, key: str) -> AttrExpression:
        """Return an attribute expression representing ``logic_group[key]``.

        Args:
            key: Attribute/key name.

        Returns:
            AttrExpression: Expression representing the attribute access.
        """

    def __getattr__(self, key: str) -> AttrExpression:
        """Return an attribute expression representing ``logic_group.key``.

        Args:
            key: Attribute name.

        Returns:
            AttrExpression: Expression representing the attribute access.
        """

    # Arithmetic operators produce MathExpression
    def __add__(self, other: Any) -> MathExpression: ...

    def __sub__(self, other: Any) -> MathExpression: ...

    def __mul__(self, other: Any) -> MathExpression: ...

    def __truediv__(self, other: Any) -> MathExpression: ...

    def __floordiv__(self, other: Any) -> MathExpression: ...

    def __pow__(self, other: Any) -> MathExpression: ...

    def __neg__(self) -> MathExpression: ...

    # Comparison operators produce ComparisonExpression
    def __eq__(self, other: Any) -> ComparisonExpression: ...

    def __ne__(self, other: Any) -> ComparisonExpression: ...

    def __gt__(self, other: Any) -> ComparisonExpression: ...

    def __ge__(self, other: Any) -> ComparisonExpression: ...

    def __lt__(self, other: Any) -> ComparisonExpression: ...

    def __le__(self, other: Any) -> ComparisonExpression: ...

    # Logical operators produce LogicalExpression
    def __and__(self, other: Any) -> LogicalExpression: ...

    def __or__(self, other: Any) -> LogicalExpression: ...

    def __invert__(self) -> LogicalExpression: ...


class AttrExpression(ContextLogicExpression):
    """Expression representing a single attribute from the logic context.

    Attributes:
        attr: The attribute name referred to by the expression.
    """

    attr: str

    def __init__(self, *, attr: str, repr: str = None, **kwargs) -> None:
        """Create an attribute expression.

        The constructor automatically passes the kwargs to underlying base classes, if any.
        See ``ContextLogicExpression`` and ``LogicNode`` for more details.

        Args:
            attr: Attribute name or list of nested attribute names.
            repr: Optional textual representation override.
        """

    def __getitem__(self, key: str) -> AttrNestedExpression: ...

    def __getattr__(self, key: str) -> AttrNestedExpression: ...


class AttrNestedExpression(ContextLogicExpression):
    """Expression representing nested attribute access (a.b.c).

    Attributes:
        attrs: list of attribute path components in access order.
    """

    attrs: list[str]

    def __init__(self, *, attrs: list[str], repr: str = None, **kwargs) -> None:
        """Create a nested attribute expression.

        The constructor automatically passes the kwargs to underlying base classes, if any.
        See ``ContextLogicExpression`` and ``LogicNode`` for more details.

        Args:
            attrs: Sequence of attribute names describing the path.
        """

    def __getitem__(self, key: str) -> AttrNestedExpression: ...

    def __getattr__(self, key: str) -> AttrNestedExpression: ...


class GetterExpression(ContextLogicExpression):
    """Expression representing a single key/index access from the logic context.

    Attributes:
        key: The key/index referred to by the expression.
    """

    key: Any

    def __init__(self, *, key: Any, repr: str = None, **kwargs) -> None:
        """Create a getter expression.

        The constructor automatically passes the kwargs to underlying base classes, if any.
        See ``ContextLogicExpression`` and ``LogicNode`` for more details.

        Args:
            key: Key/index or list of nested keys/indexes.
            repr: Optional textual representation override.
        """

    def __getitem__(self, key: Any) -> GetterNestedExpression: ...


class GetterNestedExpression(ContextLogicExpression):
    """Expression representing nested key/index access (a[b][c]).

    Attributes:
        keys: list of keys/indexes in access order.
    """

    keys: list[Any]

    def __init__(self, *, keys: list[Any], repr: str = None, **kwargs) -> None:
        """Create a nested getter expression.

        The constructor automatically passes the kwargs to underlying base classes, if any.
        See ``ContextLogicExpression`` and ``LogicNode`` for more details.

        Args:
            keys: Sequence of keys/indexes describing the access path.
        """

    def __getitem__(self, key: Any) -> GetterNestedExpression: ...


class MathExpressionOperator(enum.StrEnum):
    """A pseudo-enum class representing mathematical operators for MathExpression."""

    add: MathExpressionOperator
    sub: MathExpressionOperator
    mul: MathExpressionOperator
    truediv: MathExpressionOperator
    floordiv: MathExpressionOperator
    pow: MathExpressionOperator
    neg: MathExpressionOperator

    def to_func(self) -> UNARY_OP_FUNC | BINARY_OP_FUNC: ...

    @classmethod
    def from_str(cls, op_str: str) -> MathExpressionOperator: ...


class MathExpression(ContextLogicExpression):
    """Expression representing an arithmetic operation.

    Attributes:
        left: Left operand (expression or literal).
        right: Right operand or sentinel when unary.
        dtype: Resulting data type (defaults to float).
        op_name: Internal operator name.
        op_repr: Human-readable operator symbol or function name.
    """

    left: Any
    right: Any
    op_name: str
    op_repr: str

    def __init__(
            self,
            *,
            left: Any,
            op: str | MathExpressionOperator | UNARY_OP_FUNC | BINARY_OP_FUNC,
            right: Any = NO_DEFAULT,
            **kwargs
    ) -> None:
        """Create a MathExpression.

        The constructor automatically passes the kwargs to underlying base classes, if any.
        - `repr` is automatically generated based on the operator and operands, unless overridden via kwargs.
        - `dtype` defaults to float unless specified in kwargs.

        Args:
            left: Left operand, can be an expression or a literal.
            op: Operator specifier (operator object, string, or callable).
            right: Right operand or omitted for unary operators.
            op_name: Optional operator name override.
            op_repr: Optional operator symbol override.
        """


class ComparisonExpressionOperator(enum.StrEnum):
    """A pseudo-enum class representing comparison operators for ComparisonExpression."""

    eq: ComparisonExpressionOperator
    ne: ComparisonExpressionOperator
    gt: ComparisonExpressionOperator
    ge: ComparisonExpressionOperator
    lt: ComparisonExpressionOperator
    le: ComparisonExpressionOperator

    def to_func(self) -> BINARY_OP_FUNC: ...

    @classmethod
    def from_str(cls, op_str: str) -> ComparisonExpressionOperator: ...

    @property
    def int_enum(self) -> int: ...


class ComparisonExpression(ContextLogicExpression):
    """Expression representing a comparison operation, returning only boolean result."""

    left: Any
    right: Any
    op_name: str
    op_repr: str

    def __init__(
            self,
            *,
            left: Any,
            op: Any,
            right: Any,
            **kwargs
    ) -> None:
        """Create a ComparisonExpression.

        The constructor automatically passes the kwargs to underlying base classes, if any.
        - 'repr' is automatically generated based on the operator and operands, unless overridden via kwargs.
        - 'dtype' defaults to bool unless specified in kwargs.

        Args:
            left: Left operand.
            op: Operator specifier (operator object, string, or callable).
            right: Right operand.
            op_name: Optional operator name override.
            op_repr: Optional operator symbol override.
        """


class LogicalExpressionOperator(enum.StrEnum):
    """Pseudo-enum class representing logical operators for LogicalExpression."""

    and_: LogicalExpressionOperator
    or_: LogicalExpressionOperator
    not_: LogicalExpressionOperator

    def to_func(self) -> UNARY_OP_FUNC | BINARY_OP_FUNC: ...

    @classmethod
    def from_str(cls, op_str: str) -> LogicalExpressionOperator: ...

    @property
    def int_enum(self) -> int: ...


class LogicalExpression(ContextLogicExpression):
    """Expression representing boolean logic operations.

    Attributes:
        left: Left operand (expression or literal).
        right: Right operand or sentinel for unary operations.
        dtype: Resulting data type (bool).
        op_name: Name of the logical operator.
        op_repr: Operator symbol or representation.
        repr: Human-readable representation of the expression.
    """

    left: Any
    right: Any
    op_name: str
    op_repr: str

    def __init__(
            self,
            *,
            left: Any,
            op: Any,
            right: Any = NO_DEFAULT,
            **kwargs
    ) -> None:
        """Create a LogicalExpression.

        The constructor automatically passes the kwargs to underlying base classes, if any.
        - 'repr' is automatically generated based on the operator and operands, unless overridden via kwargs.
        - 'dtype' defaults to bool unless specified in kwargs.

        Args:
            left: Left operand.
            op: Logical operator specifier (operator object, string, or callable).
            right: Optional right operand for binary operators.
            op_name: Optional operator name override.
            op_repr: Optional operator symbol override.
        """
