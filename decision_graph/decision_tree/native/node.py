from __future__ import annotations

import enum
import operator
from typing import Any, Callable

from .abc import LGM, LogicNode, LogicGroup, NO_CONDITION, AUTO_CONDITION, NodeEdgeCondition, PlaceholderNode
from .collection import LogicMapping
from ..exc import NO_DEFAULT, TooManyChildren, TooFewChildren, EdgeValueError, ContextsNotFound

UNARY_OP_FUNC = Callable[[Any], Any]
BINARY_OP_FUNC = Callable[[Any, Any], Any]


class RootLogicNode(LogicNode):
    def __init__(self, *, name: str = 'Entry Point'):
        super().__init__(expression=True, dtype=bool, repr=name)

    def _entry_check(self) -> bool:
        return True

    def _on_enter(self) -> None:
        self._append(PlaceholderNode(auto_connect=False), NO_CONDITION)
        LGM._ln_enter(self)
        LGM.shelve()
        LGM.inspection_mode = True
        LGM._ln_enter(self)

    def _on_exit(self) -> None:
        self._consolidate_placeholder()
        LGM._ln_exit(self)
        LGM.unshelve()
        LGM._ln_exit(self)

    def _append(self, child: LogicNode, condition: NodeEdgeCondition) -> None:
        if self.subordinates:
            raise TooManyChildren()
        if condition is not AUTO_CONDITION and condition is not NO_CONDITION:
            raise EdgeValueError()
        super()._append(child, NO_CONDITION)

    def eval_recursively(self, **kwargs):
        return self.child.eval_recursively(**kwargs)

    def to_html(self, file_name: str = "root.html", with_eval: bool = True):
        from ..webui import to_html
        to_html(self, file_name, with_eval)

    @property
    def child(self) -> LogicNode:
        if self.subordinates:
            return self.subordinates[0]
        raise TooFewChildren()


class ContextLogicExpression(LogicNode):
    def __init__(
            self,
            *,
            expression: float | int | bool | Exception | Callable[[], Any],
            logic_group: LogicGroup | None = None,
            dtype: type = None,
            repr: str = None
    ):
        super().__init__(expression=expression, dtype=dtype, repr=repr)

        if logic_group is None:
            logic_group = LGM.active_group
            if logic_group is None:
                raise ContextsNotFound(
                    f'Must assign a logic group or initialize {self.__class__.__name__} within a LogicGroup context!'
                )

        self.logic_group = logic_group

    @staticmethod
    def _safe_eval(v: Any) -> Any:
        if isinstance(v, LogicNode):
            return v._eval(False)
        return v

    @staticmethod
    def _safe_alias(v: Any) -> str:
        if isinstance(v, LogicNode):
            return v.repr
        return str(v)

    # --- Attribute access ---
    def __getitem__(self, key: str) -> AttrExpression:
        return AttrExpression(attr=key, logic_group=self.logic_group)

    def __getattr__(self, key: str) -> AttrExpression:
        if key.startswith('_'):
            raise AttributeError(f"'{self.__class__.__name__}' object has no attribute '{key}'")
        return AttrExpression(attr=key, logic_group=self.logic_group)

    # --- Math ---
    def __add__(self, other):
        return MathExpression(left=self, op=MathExpressionOperator.add, right=other, logic_group=self.logic_group)

    def __sub__(self, other):
        return MathExpression(left=self, op=MathExpressionOperator.sub, right=other, logic_group=self.logic_group)

    def __mul__(self, other):
        return MathExpression(left=self, op=MathExpressionOperator.mul, right=other, logic_group=self.logic_group)

    def __truediv__(self, other):
        return MathExpression(left=self, op=MathExpressionOperator.truediv, right=other, logic_group=self.logic_group)

    def __floordiv__(self, other):
        return MathExpression(left=self, op=MathExpressionOperator.floordiv, right=other, logic_group=self.logic_group)

    def __pow__(self, other):
        return MathExpression(left=self, op=MathExpressionOperator.pow, right=other, logic_group=self.logic_group)

    def __neg__(self):
        return MathExpression(left=self, op=MathExpressionOperator.neg, repr=f'-{self.repr}', logic_group=self.logic_group)

    # --- Comparisons ---
    def __eq__(self, other):
        return ComparisonExpression(left=self, op=ComparisonExpressionOperator.eq, right=other, logic_group=self.logic_group)

    def __ne__(self, other):
        return ComparisonExpression(left=self, op=ComparisonExpressionOperator.ne, right=other, logic_group=self.logic_group)

    def __gt__(self, other):
        return ComparisonExpression(left=self, op=ComparisonExpressionOperator.gt, right=other, logic_group=self.logic_group)

    def __ge__(self, other):
        return ComparisonExpression(left=self, op=ComparisonExpressionOperator.ge, right=other, logic_group=self.logic_group)

    def __lt__(self, other):
        return ComparisonExpression(left=self, op=ComparisonExpressionOperator.lt, right=other, logic_group=self.logic_group)

    def __le__(self, other):
        return ComparisonExpression(left=self, op=ComparisonExpressionOperator.le, right=other, logic_group=self.logic_group)

    # --- Logical ---
    def __and__(self, other):
        return LogicalExpression(left=self, op=LogicalExpressionOperator.and_, right=other, logic_group=self.logic_group)

    def __or__(self, other):
        return LogicalExpression(left=self, op=LogicalExpressionOperator.or_, right=other, logic_group=self.logic_group)

    def __invert__(self):
        return LogicalExpression(left=self, op=LogicalExpressionOperator.not_, repr=f'~{self.repr}', logic_group=self.logic_group)


class AttrExpression(ContextLogicExpression):
    def __init__(
            self,
            *,
            attr: str,
            expression: float | int | bool | Exception | Callable[[], Any] = None,
            logic_group: LogicGroup | None = None,
            dtype: type = None,
            repr: str = None
    ):
        super().__init__(
            expression=self.eval if expression is None else expression,
            logic_group=logic_group,
            dtype=dtype,
            repr=f'{self.logic_group.name}.{attr}' if repr is None else repr
        )

        self.attr = attr

    def _eval(self, enforce_dtype: bool) -> Any:
        if isinstance(self.logic_group, LogicMapping):
            return self.logic_group._get(self.attr)
        else:
            if self.attr in self.logic_group.contexts:
                return self.logic_group.contexts[self.attr]
            raise AttributeError(f'Attribute {self.attr} does not exist in {self.logic_group}')

    def __getitem__(self, key: str) -> AttrNestedExpression:
        return AttrNestedExpression(attrs=[self.attr, key], logic_group=self.logic_group)

    def __getattr__(self, key: str) -> AttrNestedExpression:
        if key.startswith('_'):
            raise AttributeError(f"'{self.__class__.__name__}' object has no attribute '{key}'")
        return AttrNestedExpression(attrs=[self.attr, key], logic_group=self.logic_group)


class AttrNestedExpression(ContextLogicExpression):
    def __init__(
            self,
            *,
            attrs: list[str],
            expression: float | int | bool | Exception | Callable[[], Any] = None,
            logic_group: LogicGroup | None = None,
            dtype: type = None,
            repr: str = None
    ):
        super().__init__(
            expression=self.eval if expression is None else expression,
            logic_group=logic_group,
            dtype=dtype,
            repr=f'{self.logic_group.name}.{".".join(attrs)}' if repr is None else repr
        )

        self.attrs = attrs

    def _eval(self, enforce_dtype: bool) -> Any:
        if isinstance(self.logic_group, LogicMapping):
            mapping = self.logic_group.data
        else:
            mapping = self.logic_group.contexts

        for attr in self.attrs:
            mapping = mapping[attr]
        return mapping

    def __getitem__(self, key: str) -> AttrNestedExpression:
        return AttrNestedExpression(attrs=self.attrs + [key], logic_group=self.logic_group)

    def __getattr__(self, key: str) -> AttrNestedExpression:
        if key.startswith('_'):
            raise AttributeError(f"'{self.__class__.__name__}' object has no attribute '{key}'")
        return AttrNestedExpression(attrs=self.attrs + [key], logic_group=self.logic_group)


# Python does not enforce strict typing. So the Getter*Expression classes are just aliases.
GetterExpression = AttrExpression
GetterNestedExpression = AttrNestedExpression


class MathExpressionOperator(enum.StrEnum):
    add = '+'
    sub = '-'
    mul = '*'
    truediv = '/'
    floordiv = '//'
    pow = '**'
    neg = '-'

    def to_func(self) -> Callable:
        return getattr(operator, self.name)

    @classmethod
    def from_str(cls, op_str: str) -> MathExpressionOperator:
        for op in list(cls):
            op: MathExpressionOperator
            if op.name == op_str or op.value == op_str:
                return op
        raise ValueError(f'Unknown MathExpressionOperator: {op_str}')


class ComparisonExpressionOperator(enum.StrEnum):
    eq = '=='
    ne = '!='
    gt = '>'
    ge = '>='
    lt = '<'
    le = '<='

    def to_func(self) -> Callable:
        return getattr(operator, self.name)

    @classmethod
    def from_str(cls, op_str: str) -> ComparisonExpressionOperator:
        for op in list(cls):
            op: ComparisonExpressionOperator
            if op.name == op_str or op.value == op_str:
                return op
        raise ValueError(f'Unknown ComparisonExpressionOperator: {op_str}')


class LogicalExpressionOperator(enum.StrEnum):
    and_ = '&'
    or_ = '|'
    not_ = '~'

    def to_func(self) -> Callable:
        return getattr(operator, self.name)

    @classmethod
    def from_str(cls, op_str: str) -> LogicalExpressionOperator:
        for op in list(cls):
            op: LogicalExpressionOperator
            if op.name == op_str or op.value == op_str:
                return op
        raise ValueError(f'Unknown LogicalExpressionOperator: {op_str}')


class MathExpression(ContextLogicExpression):
    def __init__(
            self,
            *,
            left: Any,
            op: UNARY_OP_FUNC | BINARY_OP_FUNC | MathExpressionOperator | str,
            right: Any = NO_DEFAULT,
            expression: float | int | bool | Exception | Callable[[], Any] = NO_DEFAULT,
            logic_group: LogicGroup | None = None,
            dtype: type = None,
            repr: str = None,
            **kwargs
    ):
        self.left = left
        self.right = right

        if isinstance(op, MathExpressionOperator):
            self.op_name = kwargs.get('op_name', op.name)
            self.op_repr = kwargs.get('op_repr', op.value)
            self.op_func = op.to_func()
            repr = repr or self._op_style_repr()
        elif isinstance(op, str):
            op_obj = MathExpressionOperator.from_str(op)
            self.op_name = kwargs.get('op_name', op_obj.name)
            self.op_repr = kwargs.get('op_repr', op_obj.value)
            self.op_func = op_obj.to_func()
            repr = repr or self._op_style_repr()
        elif callable(op):
            self.op_name = op.__name__
            self.op_repr = op.__name__
            self.op_func = op
            repr = repr or self._func_style_repr()
        else:
            raise TypeError(f'Expected op to be MathExpressionOperator, str or callable, got {type(op).__name__}.')

        super().__init__(
            expression=self.eval if expression is NO_DEFAULT else expression,
            logic_group=logic_group,
            dtype=float if dtype is None else dtype,
            repr=repr,
        )

    def _op_style_repr(self) -> str:
        if self.right is NO_DEFAULT:
            return f'{self.op_repr}{self._safe_alias(self.left)}'
        return f'{self._safe_alias(self.left)} {self.op_repr} {self._safe_alias(self.right)}'

    def _func_style_repr(self) -> str:
        if self.right is NO_DEFAULT:
            return f'{self.op_repr}({ContextLogicExpression.c_safe_alias(self.left)})'
        return f'{self.op_repr}({ContextLogicExpression.c_safe_alias(self.left)}, {ContextLogicExpression.c_safe_alias(self.right)})'

    def _eval(self, enforce_dtype: bool) -> Any:
        left_val = self._safe_eval(self.left)
        if self.right is NO_DEFAULT:
            return self.op_func(left_val)
        right_val = self._safe_eval(self.right)
        return self.op_func(left_val, right_val)


class ComparisonExpression(ContextLogicExpression):
    def __init__(
            self,
            *,
            left: Any,
            op: UNARY_OP_FUNC | BINARY_OP_FUNC | ComparisonExpressionOperator | str,
            right: Any = NO_DEFAULT,
            expression: float | int | bool | Exception | Callable[[], Any] = NO_DEFAULT,
            logic_group: LogicGroup | None = None,
            dtype: type = None,
            repr: str = None,
            **kwargs
    ):
        self.left = left
        self.right = right

        if isinstance(op, ComparisonExpressionOperator):
            self.op_name = kwargs.get('op_name', op.name)
            self.op_repr = kwargs.get('op_repr', op.value)
            self.op_func = op.to_func()
            repr = repr or self._op_style_repr()
        elif isinstance(op, str):
            op_obj = ComparisonExpressionOperator.from_str(op)
            self.op_name = kwargs.get('op_name', op_obj.name)
            self.op_repr = kwargs.get('op_repr', op_obj.value)
            self.op_func = op_obj.to_func()
            repr = repr or self._op_style_repr()
        elif callable(op):
            self.op_name = op.__name__
            self.op_repr = op.__name__
            self.op_func = op
            repr = repr or self._func_style_repr()
        else:
            raise TypeError(f'Expected op to be ComparisonExpressionOperator, str or callable, got {type(op).__name__}.')

        super().__init__(
            expression=self.eval if expression is NO_DEFAULT else expression,
            logic_group=logic_group,
            dtype=bool if dtype is None else dtype,
            repr=repr,
        )

    def _op_style_repr(self) -> str:
        if self.right is NO_DEFAULT:
            return f'{self.op_repr}{self._safe_alias(self.left)}'
        return f'{self._safe_alias(self.left)} {self.op_repr} {self._safe_alias(self.right)}'

    def _func_style_repr(self) -> str:
        if self.right is NO_DEFAULT:
            return f'{self.op_repr}({ContextLogicExpression.c_safe_alias(self.left)})'
        return f'{self.op_repr}({ContextLogicExpression.c_safe_alias(self.left)}, {ContextLogicExpression.c_safe_alias(self.right)})'

    def _eval(self, enforce_dtype: bool) -> bool:
        left_val = self._safe_eval(self.left)
        if self.right is NO_DEFAULT:
            return self.op_func(left_val)
        right_val = self._safe_eval(self.right)
        return self.op_func(left_val, right_val)


class LogicalExpression(ContextLogicExpression):
    def __init__(
            self,
            *,
            left: Any,
            op: UNARY_OP_FUNC | BINARY_OP_FUNC | LogicalExpressionOperator | str,
            right: Any = NO_DEFAULT,
            expression: float | int | bool | Exception | Callable[[], Any] = NO_DEFAULT,
            logic_group: LogicGroup | None = None,
            dtype: type = None,
            repr: str = None,
            **kwargs
    ):
        self.left = left
        self.right = right

        if isinstance(op, LogicalExpressionOperator):
            self.op_name = kwargs.get('op_name', op.name)
            self.op_repr = kwargs.get('op_repr', op.value)
            self.op_func = op.to_func()
            repr = repr or self._op_style_repr()
        elif isinstance(op, str):
            op_obj = LogicalExpressionOperator.from_str(op)
            self.op_name = kwargs.get('op_name', op_obj.name)
            self.op_repr = kwargs.get('op_repr', op_obj.value)
            self.op_func = op_obj.to_func()
            repr = repr or self._op_style_repr()
        elif callable(op):
            self.op_name = op.__name__
            self.op_repr = op.__name__
            self.op_func = op
            repr = repr or self._func_style_repr()
        else:
            raise TypeError(f'Expected op to be LogicalExpressionOperator, str or callable, got {type(op).__name__}.')

        super().__init__(
            expression=self.eval if expression is NO_DEFAULT else expression,
            logic_group=logic_group,
            dtype=bool if dtype is None else dtype,
            repr=repr,
        )

    def _op_style_repr(self) -> str:
        if self.right is NO_DEFAULT:
            return f'{self.op_repr}{self._safe_alias(self.left)}'
        return f'{self._safe_alias(self.left)} {self.op_repr} {self._safe_alias(self.right)}'

    def _func_style_repr(self) -> str:
        if self.right is NO_DEFAULT:
            return f'{self.op_repr}({ContextLogicExpression.c_safe_alias(self.left)})'
        return f'{self.op_repr}({ContextLogicExpression.c_safe_alias(self.left)}, {ContextLogicExpression.c_safe_alias(self.right)})'

    def _eval(self, enforce_dtype: bool) -> bool:
        left_val = self._safe_eval(self.left)
        if self.right is NO_DEFAULT:
            return self.op_func(left_val)
        right_val = self._safe_eval(self.right)
        return self.op_func(left_val, right_val)
