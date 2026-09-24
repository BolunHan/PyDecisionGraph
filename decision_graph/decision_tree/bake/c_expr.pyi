"""The operator layer: the expressions a graph computes with.

An expression is an operator over operands, and its operands are nodes: a
literal, a read, another expression. The node its operands are bound to holds
them - a slot that refers to an operand reads that operand's storage, so the
expression keeps the node alive for as long as it reads through it.

The arity is the class: a unary node takes one operand, a binary two, a ternary
three, and a call as many as it is given.

A composition written with a Python value puts a literal in that operand's place:
``5 + read`` builds the same node as ``ConstantNode(5) + read``, and ``read - 5``
the same as ``read - ConstantNode(5)``.
"""

import enum
from typing import Any

from .c_node import LogicNode


class ExpressionOperator(enum.IntEnum):
    """The op codes an expression node can carry.

    One member per code the C layer knows, family by family, with the arithmetic
    and comparison symbols spelled as the operators they are. The enum is the
    Python face of the C op codes: a cimported C constant is not a module
    attribute, so without it a caller would pass bare numbers.
    """

    none: int
    arith: int
    add: int
    sub: int
    mul: int
    div: int
    floordiv: int
    pow: int
    neg: int
    compare: int
    eq: int
    ne: int
    gt: int
    ge: int
    lt: int
    le: int
    logic: int
    and_: int
    or_: int
    not_: int
    access: int
    attr: int
    getitem: int


class ExpressionNode(LogicNode):
    """An operator over operands, of an arity the type does not fix.

    The operands are the nodes its arguments read from, and this wrapper HOLDS
    them: an argument that refers to a node reads that node's storage, so a
    released operand would leave the expression reading a block that is gone.
    """

    def __init__(self, n_args: int = ..., node_type: int = ...) -> None:
        """Build an expression of an explicit arity and node type.

        Args:
            n_args: How many operands it takes.
            node_type: The C node type this expression is.

        Raises:
            MemoryError: When the block cannot be allocated.
        """
        ...

    def bind(self, index: int, node: LogicNode) -> None:
        """Bind an operand to one of this expression's argument slots.

        Args:
            index: Argument position to bind.
            node: Node whose value the argument takes.

        Raises:
            RuntimeError: When the C layer refuses the binding.
        """
        ...

    def __add__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a sum expression with another node."""
        ...

    def __radd__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a sum expression with a Python value on the left."""
        ...

    def __sub__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a difference expression with another node."""
        ...

    def __rsub__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a difference expression with a Python value on the left."""
        ...

    def __mul__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a product expression with another node."""
        ...

    def __rmul__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a product expression with a Python value on the left."""
        ...

    def __truediv__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a division expression with another node."""
        ...

    def __rtruediv__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a division expression with a Python value on the left."""
        ...

    def __floordiv__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a floor-division expression with another node."""
        ...

    def __rfloordiv__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a floor-division expression with a Python value on the left."""
        ...

    def __pow__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a power expression with another node."""
        ...

    def __rpow__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a power expression with a Python value on the left."""
        ...

    def __neg__(self) -> UnaryExpression:
        """Compose a negation of this expression."""
        ...

    def __eq__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose an equality CONDITION with another node."""
        ...

    def __ne__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose an inequality condition with another node."""
        ...

    def __hash__(self) -> int:
        """Hash by the C block's address, so a node can key a dict."""
        ...

    def __lt__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a less-than condition with another node."""
        ...

    def __le__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a less-or-equal condition with another node."""
        ...

    def __gt__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a greater-than condition with another node."""
        ...

    def __ge__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a greater-or-equal condition with another node."""
        ...

    def __and__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a conjunction with another node."""
        ...

    def __rand__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a conjunction with a Python value on the left."""
        ...

    def __or__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a disjunction with another node."""
        ...

    def __ror__(self, other: LogicNode | bool | int | float | str) -> BinaryExpression:
        """Compose a disjunction with a Python value on the left."""
        ...

    def __invert__(self) -> UnaryExpression:
        """Compose the negation of this expression as a condition."""
        ...

    @property
    def op(self) -> ExpressionOperator:
        """The operator this expression applies."""
        ...

    @property
    def n_args(self) -> int:
        """How many operands the expression takes."""
        ...

    @property
    def operands(self) -> list[LogicNode | None]:
        """The nodes the arguments read from, in argument order.

        An entry is None where the argument reads from nothing this wrapper
        holds - an argument a value was folded into, whose operand node is gone
        by design.
        """
        ...


class UnaryExpression(ExpressionNode):
    """An expression over one operand."""

    def __init__(self, op: ExpressionOperator, src: LogicNode) -> None:
        """Build a one-operand expression.

        Args:
            op: The operator to apply.
            src: The operand it applies to.

        Raises:
            MemoryError: When the block cannot be allocated.
        """
        ...


class BinaryExpression(ExpressionNode):
    """An expression over two operands: the arithmetic and comparison shape."""

    def __init__(self, op: ExpressionOperator, var_0: LogicNode, var_1: LogicNode) -> None:
        """Build a two-operand expression.

        Args:
            op: The operator to apply.
            var_0: The left operand.
            var_1: The right operand.

        Raises:
            MemoryError: When the block cannot be allocated.
        """
        ...


class TernaryExpression(ExpressionNode):
    """An expression over three operands: the conditional shape."""

    def __init__(self, op: ExpressionOperator, var_0: LogicNode, var_1: LogicNode, var_2: LogicNode) -> None:
        """Build a three-operand expression.

        Args:
            op: The operator to apply.
            var_0: The first operand.
            var_1: The second operand.
            var_2: The third operand.

        Raises:
            MemoryError: When the block cannot be allocated.
        """
        ...


class CallExpression(ExpressionNode):
    """An expression over any number of operands, written as a call.

    The callee's name is display text, composed into the node's ``repr``: it is
    not a field of the block, which is why a reconstruction brings a call back as
    the generic node view rather than as this class.
    """

    def __init__(self, op: ExpressionOperator, inputs: Any, name: str | None = None) -> None:
        """Build a call-shaped expression.

        Args:
            op: The operator to apply.
            inputs: The arguments, in order.
            name: The callee's name, for the node's display text.

        Raises:
            TypeError: When an argument is not a node.
            MemoryError: When the block cannot be allocated.
        """
        ...
