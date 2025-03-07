from __future__ import annotations

from abc import ABCMeta
from collections.abc import Callable
from typing import Any, Self

from .abc import LogicGroup, LogicNode, ActionNode, LGM, NO_CONDITION
from .exc import TooManyChildren, TooFewChildren
from .expression import ContextLogicExpression as _CLE, AttrExpression as _AE, MathExpression as _ME, ComparisonExpression as _CE, LogicalExpression as _LE

__all__ = ['NoAction', 'LongAction', 'ShortAction', 'RootLogicNode', 'ContextLogicExpression', 'AttrExpression', 'MathExpression', 'ComparisonExpression', 'LogicalExpression']


class NoAction(ActionNode):
    def __init__(self, auto_connect: bool = True):
        super().__init__(
            repr='<NoAction>',
            auto_connect=auto_connect
        )

        self.sig = 0

    def eval(self, enforce_dtype: bool = False) -> ActionNode:
        return self


class LongAction(ActionNode):
    def __init__(self, sig: int = 1, auto_connect: bool = True):
        super().__init__(
            repr=f'<LongAction>(sig = {sig})',
            auto_connect=auto_connect
        )

        self.sig = sig

    def eval(self, enforce_dtype: bool = False) -> ActionNode:
        return self


class ShortAction(ActionNode):
    def __init__(self, sig: int = -1, auto_connect: bool = True):
        super().__init__(
            repr=f'<ShortAction>(sig = {sig})',
            auto_connect=auto_connect
        )

        self.sig = sig

    def eval(self, enforce_dtype: bool = False) -> ActionNode:
        return self


class RootLogicNode(LogicNode):
    def __init__(self):
        super().__init__(
            expression=True,
            repr=f'Entry Point'
        )

    def _entry_check(self):
        return True

    def _on_enter(self):
        # pre-shelve call
        LGM.enter_expression(node=self)

        state = LGM.shelve()

        state['inspection_mode'] = LGM.inspection_mode

        LGM.inspection_mode = True

        # post-shelve call
        LGM._active_nodes.append(self)

    def _on_exit(self):
        # pre-unshelve call
        # LGM.exit_expression(node=self)

        state = LGM.unshelve()

        # post-unshelve call
        LGM.exit_expression(node=self)

        LGM.inspection_mode = state['inspection_mode']

    def append(self, expression: Self, edge_condition: Any = None):
        if self.nodes:
            raise TooManyChildren()
        super().append(expression=expression, edge_condition=NO_CONDITION)

    def eval_recursively(self, **kwargs):
        return self.child.eval_recursively(**kwargs)

    def to_html(self, with_group=True, dry_run=True, filename="decision_graph.html", **kwargs):
        return self.child.to_html(with_group=with_group, dry_run=dry_run, filename=filename, **kwargs)

    @property
    def child(self) -> LogicNode:
        if self.nodes:
            return self.last_node

        raise TooFewChildren()


class ContextLogicExpression(_CLE, LogicNode, metaclass=ABCMeta):
    def __init__(self, expression: float | int | bool | Exception | Callable[[], Any], dtype: type = None, repr: str = None, logic_group: LogicGroup = None):
        super().__init__(expression=expression, dtype=dtype, repr=repr, logic_group=logic_group)
        LogicNode.__init__(self=self, expression=expression, dtype=dtype, repr=repr)

    # magic method to invoke AttrExpression
    def __getitem__(self, key: str) -> AttrExpression:
        return AttrExpression(attr=key, logic_group=self.logic_group)

    def __getattr__(self, key: str) -> AttrExpression:
        return AttrExpression(attr=key, logic_group=self.logic_group)

    # math operation to invoke MathExpression

    def __add__(self, other: int | float | bool | Self) -> Self:
        return MathExpression(left=self, op=MathExpression.Operator.add, right=other, logic_group=self.logic_group)

    def __sub__(self, other: int | float | bool | Self) -> Self:
        return MathExpression(left=self, op=MathExpression.Operator.sub, right=other, logic_group=self.logic_group)

    def __mul__(self, other: int | float | bool | Self) -> Self:
        return MathExpression(left=self, op=MathExpression.Operator.mul, right=other, logic_group=self.logic_group)

    def __truediv__(self, other: int | float | bool | Self) -> Self:
        return MathExpression(left=self, op=MathExpression.Operator.truediv, right=other, logic_group=self.logic_group)

    def __floordiv__(self, other: int | float | bool | Self) -> Self:
        return MathExpression(left=self, op=MathExpression.Operator.floordiv, right=other, logic_group=self.logic_group)

    def __pow__(self, other: int | float | bool | Self) -> Self:
        return MathExpression(left=self, op=MathExpression.Operator.pow, right=other, logic_group=self.logic_group)

    def __neg__(self):
        return MathExpression(left=self, op=MathExpression.Operator.neg, repr=f'-{self.repr}', logic_group=self.logic_group)

    # Comparison operation to invoke ComparisonExpression

    def __eq__(self, other: int | float | bool | str | Self) -> Self:
        return ComparisonExpression(left=self, op=ComparisonExpression.Operator.eq, right=other, logic_group=self.logic_group)

    def __ne__(self, other: int | float | bool | str | Self) -> Self:
        return ComparisonExpression(left=self, op=ComparisonExpression.Operator.ne, right=other, logic_group=self.logic_group)

    def __gt__(self, other: int | float | bool | Self) -> Self:
        return ComparisonExpression(left=self, op=ComparisonExpression.Operator.gt, right=other, logic_group=self.logic_group)

    def __ge__(self, other: int | float | bool | Self) -> Self:
        return ComparisonExpression(left=self, op=ComparisonExpression.Operator.ge, right=other, logic_group=self.logic_group)

    def __lt__(self, other: int | float | bool | Self) -> Self:
        return ComparisonExpression(left=self, op=ComparisonExpression.Operator.lt, right=other, logic_group=self.logic_group)

    def __le__(self, other: int | float | bool | Self) -> Self:
        return ComparisonExpression(left=self, op=ComparisonExpression.Operator.le, right=other, logic_group=self.logic_group)

    # Logical operation to invoke LogicalExpression

    def __and__(self, other: int | float | bool | Self) -> Self:
        return LogicalExpression(left=self, op=LogicalExpression.Operator.and_, right=other, logic_group=self.logic_group)

    def __or__(self, other: Self | bool) -> Self:
        return LogicalExpression(left=self, op=LogicalExpression.Operator.or_, right=other, logic_group=self.logic_group)

    def __invert__(self) -> Self:
        return LogicalExpression(left=self, op=LogicalExpression.Operator.not_, repr=f'~{self.repr}', logic_group=self.logic_group)


class AttrExpression(_AE, ContextLogicExpression):
    def __init__(self, attr: str | int, dtype: type = None, repr: str = None, logic_group: LogicGroup = None, edge_condition: Any = True):
        super().__init__(attr=attr, repr=repr, logic_group=logic_group)
        ContextLogicExpression.__init__(self=self, expression=self._eval, dtype=self.dtype, repr=self.repr, logic_group=self.logic_group)


class MathExpression(_ME, ContextLogicExpression):
    def __init__(self, left: ContextLogicExpression | int | float, op: _ME.Operator, right: ContextLogicExpression | int | float = None, dtype: type = None, repr: str = None, logic_group: LogicGroup = None, edge_condition: Any = True):
        super().__init__(left=left, op=op, right=right, dtype=dtype, repr=repr, logic_group=logic_group)
        ContextLogicExpression.__init__(self=self, expression=self._eval, dtype=self.dtype, repr=self.repr, logic_group=self.logic_group)


class ComparisonExpression(_CE, ContextLogicExpression):
    def __init__(self, left: ContextLogicExpression | int | float, op: _CE.Operator, right: ContextLogicExpression | int | float = None, dtype: type = None, repr: str = None, logic_group: LogicGroup = None, edge_condition: Any = True):
        super().__init__(left=left, op=op, right=right, dtype=dtype, repr=repr, logic_group=logic_group)
        ContextLogicExpression.__init__(self=self, expression=self._eval, dtype=self.dtype, repr=self.repr, logic_group=self.logic_group)


class LogicalExpression(_LE, ContextLogicExpression):
    def __init__(self, left: ContextLogicExpression | int | float, op: _LE.Operator, right: ContextLogicExpression | int | float = None, dtype: type = None, repr: str = None, logic_group: LogicGroup = None, edge_condition: Any = True):
        super().__init__(left=left, op=op, right=right, dtype=dtype, repr=repr, logic_group=logic_group)
        ContextLogicExpression.__init__(self=self, expression=self._eval, dtype=self.dtype, repr=self.repr, logic_group=self.logic_group)
