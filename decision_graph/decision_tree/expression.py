from __future__ import annotations

import abc
import builtins
import enum
import importlib
import json
import operator
import traceback
from collections.abc import Callable, Mapping
from typing import Any, Self

from .abc import LogicGroup, LGM, LogicExpression, ExpressionCollection
from .exc import ContextsNotFound

__all__ = ['ContextLogicExpression', 'AttrExpression', 'MathExpression', 'ComparisonExpression', 'LogicalExpression']


class ContextLogicExpression(LogicExpression, metaclass=abc.ABCMeta):
    def __init__(self, expression: float | int | bool | Exception | Callable[[], Any], dtype: type = None, repr: str = None, logic_group: LogicGroup = None):
        if logic_group is None:
            if not LGM.active_logic_group is None:
                logic_group = LGM.active_logic_group
            else:
                raise ContextsNotFound(f'Must assign a logic group or initialize {self.__class__.__name__} with in a LogicGroup with statement!')

        super().__init__(expression=expression, dtype=dtype, repr=repr)

        self.logic_group = logic_group

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

    def __bool__(self) -> bool:
        return bool(self.eval())

    @abc.abstractmethod
    def to_json(self, fmt='dict') -> dict | str:
        ...

    @classmethod
    @abc.abstractmethod
    def from_json(cls, json_message: str | bytes | dict) -> Self:
        ...

    @classmethod
    def dtype_to_str(cls, dtype: type) -> str:
        if dtype is None or dtype is Any:
            return 'null'

        # Check if the type is in built-in types
        if dtype in vars(builtins).values():
            return dtype.__name__

        # For non-built-in types, construct the fully qualified name
        module = dtype.__module__
        qualname = dtype.__qualname__
        return f"{module}.{qualname}"

    @classmethod
    def str_to_dtype(cls, type_name: str) -> type:
        if type_name == 'null':  # Handle the special case for `None` or `Any`
            return type(None)

        # Check if it's a built-in type
        if hasattr(builtins, type_name):
            return getattr(builtins, type_name)

        # For non-built-in types, split the string into module and class name
        parts = type_name.rsplit('.', 1)
        if len(parts) != 2:
            raise ValueError(f"Invalid type name: {type_name}")

        module_name, class_name = parts
        try:
            # Import the module and get the type
            module = importlib.import_module(module_name)
            return getattr(module, class_name)
        except (ImportError, AttributeError) as e:
            raise ValueError(f"Failed to deserialize type '{type_name}': {e}")

    @classmethod
    def safe_alias(cls, v: ContextLogicExpression | int | float | bool) -> str:
        if isinstance(v, ContextLogicExpression):
            return v.repr

        return str(v)

    @classmethod
    def safe_dump(cls, v: ContextLogicExpression | int | float | bool) -> dict[str, Any] | Any:
        if isinstance(v, ContextLogicExpression):
            return v.to_json(fmt='dict')

        return v

    @classmethod
    def safe_load(cls, d: dict[str, Any] | Any) -> ContextLogicExpression | int | float | bool:
        if isinstance(d, dict) and 'expression_type' in d:
            constructor = globals()[d['expression_type']]
            return constructor.from_json(d)

        return d

    @classmethod
    def safe_eval(cls, v: ContextLogicExpression | int | float | bool) -> Any:
        if isinstance(v, ContextLogicExpression):
            return v.eval()

        return v


class AttrExpression(ContextLogicExpression):
    def __init__(self, attr: str | int, dtype: type = None, repr: str = None, logic_group: LogicGroup = None):
        self.attr = attr

        super().__init__(
            expression=self._eval,
            dtype=dtype,
            repr=repr if repr is not None else f'{logic_group.name}.{attr}',
            logic_group=logic_group
        )

    def _eval(self) -> Any:
        if isinstance(self.logic_group, LogicGroup) and self.attr in self.logic_group.contexts:
            return self.logic_group.contexts[self.attr]
        elif isinstance(self.logic_group, ExpressionCollection):  # for indexing purpose, will not check attr existence.
            return self.logic_group.contexts['data'][self.attr]
        elif isinstance(self.logic_group, Mapping) and self.attr in self.logic_group:
            return self.logic_group[self.attr]
        elif hasattr(self.logic_group, self.attr):
            return getattr(self.logic_group, self.attr)
        else:
            try:
                self.logic_group[self.attr]
            except:
                raise AttributeError(f'Attribute {self.attr} does not exist in {self.logic_group}!\n{traceback.format_exc()}')

    def to_json(self, fmt='dict') -> dict | str:
        json_dict = dict(
            expression_type=self.__class__.__name__,
            attr=self.attr,
            repr=self.repr
        )

        if self.dtype is not None and self.dtype is not Any:
            json_dict['dtype'] = self.dtype_to_str(self.dtype)

        if fmt == 'dict':
            return json_dict
        else:
            return json.dumps(json_dict)

    @classmethod
    def from_json(cls, json_message: str | bytes | dict) -> Self:
        if isinstance(json_message, dict):
            json_dict = json_message
        else:
            json_dict = json.loads(json_message)

        assert json_dict['expression_type'] == cls.__name__

        kwargs = dict(
            attr=json_dict['attr'],
            repr=json_dict['repr'],
        )

        if 'dtype' in json_dict:
            kwargs['dtype'] = cls.str_to_dtype(json_dict['dtype'])

        self = cls(
            **kwargs
        )

        return self


class MathExpression(ContextLogicExpression):
    class Operator(enum.StrEnum):
        add = '+'
        sub = '-'
        mul = '*'
        truediv = '/'
        floordiv = '//'
        pow = '**'
        neg = '--'

    def __init__(self, left: ContextLogicExpression | int | float, op: Operator, right: ContextLogicExpression | int | float = None, dtype: type = None, repr: str = None, logic_group: LogicGroup = None):
        self.op = op
        self.left = left
        self.right = right

        super().__init__(
            expression=self._eval,
            dtype=dtype,
            repr=repr if repr is not None else
            f'-{self.safe_alias(left)}' if self.op is self.Operator.neg else
            f'{self.safe_alias(left)} {self.op} {self.safe_alias(right)}',
            logic_group=logic_group
        )

    def _eval(self) -> Any:
        op = getattr(operator, self.op.name)
        res = op(self.safe_eval(self.left), self.safe_eval(self.right))
        return res

    def _neg(self) -> Any:
        res = -self.safe_eval(self.left)
        return res

    def to_json(self, fmt='dict') -> dict | str:
        json_dict = dict(
            expression_type=self.__class__.__name__,
            left=self.safe_dump(self.left),
            op=self.op.name,
            repr=self.repr
        )

        if self.right is not None:
            json_dict['right'] = self.safe_dump(self.right)

        if self.dtype is not None and self.dtype is not Any:
            json_dict['dtype'] = self.dtype_to_str(self.dtype)

        if fmt == 'dict':
            return json_dict
        else:
            return json.dumps(json_dict)

    @classmethod
    def from_json(cls, json_message: str | bytes | dict) -> Self:
        if isinstance(json_message, dict):
            json_dict = json_message
        else:
            json_dict = json.loads(json_message)

        assert json_dict['expression_type'] == cls.__name__

        kwargs = dict(
            left=cls.safe_load(json_dict['left']),
            op=cls.Operator[json_dict['op']],
            repr=json_dict['repr'],
        )

        if 'right' in json_dict:
            kwargs['right'] = cls.safe_load(json_dict['right'])

        if 'dtype' in json_dict:
            kwargs['dtype'] = cls.str_to_dtype(json_dict['dtype'])

        self = cls(
            **kwargs
        )

        return self


class ComparisonExpression(ContextLogicExpression):
    class Operator(enum.StrEnum):
        eq = '=='
        ne = '!='
        gt = '>'
        ge = '>='
        lt = '<'
        le = '<='

    def __init__(self, left: ContextLogicExpression | int | float, op: Operator, right: ContextLogicExpression | int | float = None, dtype: type = None, repr: str = None, logic_group: LogicGroup = None):
        self.op = op
        self.left = left
        self.right = right

        super().__init__(
            expression=self._eval,
            dtype=dtype,
            repr=repr if repr is not None else f'{self.safe_alias(left)} {self.op} {self.safe_alias(right)}',
            logic_group=logic_group
        )

    def _eval(self) -> Any:
        left = self.safe_eval(self.left)
        right = self.safe_eval(self.right)

        match self.op:
            case self.Operator.eq:
                return left == right
            case self.Operator.ne:
                return left != right
            case self.Operator.gt:
                return left > right
            case self.Operator.ge:
                return left >= right
            case self.Operator.lt:
                return left < right
            case self.Operator.le:
                return left <= right
            case _:
                raise NotImplementedError(f'Invalid comparison operator {self.op}!')

    def to_json(self, fmt='dict') -> dict | str:
        json_dict = dict(
            expression_type=self.__class__.__name__,
            left=self.safe_dump(self.left),
            right=self.safe_dump(self.right),
            op=self.op.name,
            repr=self.repr
        )

        if self.dtype is not None and self.dtype is not Any:
            json_dict['dtype'] = self.dtype_to_str(self.dtype)

        if fmt == 'dict':
            return json_dict
        else:
            return json.dumps(json_dict)

    @classmethod
    def from_json(cls, json_message: str | bytes | dict) -> Self:
        if isinstance(json_message, dict):
            json_dict = json_message
        else:
            json_dict = json.loads(json_message)

        assert json_dict['expression_type'] == cls.__name__

        kwargs = dict(
            left=cls.safe_load(json_dict['left']),
            right=cls.safe_load(json_dict['right']),
            op=cls.Operator[json_dict['op']],
            repr=json_dict['repr'],
        )

        if 'dtype' in json_dict:
            kwargs['dtype'] = cls.str_to_dtype(json_dict['dtype'])

        self = cls(
            **kwargs
        )

        return self


class LogicalExpression(ContextLogicExpression):
    class Operator(enum.StrEnum):
        and_ = '&'
        or_ = '|'
        not_ = '~'

    def __init__(self, left: ContextLogicExpression | int | float, op: Operator, right: ContextLogicExpression | int | float = None, dtype: type = None, repr: str = None, logic_group: LogicGroup = None):
        self.op = op
        self.left = left
        self.right = right

        super().__init__(
            expression=self._eval,
            dtype=dtype,
            repr=repr if repr is not None else
            f'~{self.safe_alias(left)}' if self.op is self.Operator.not_ else
            f'{self.safe_alias(left)} {self.op} {self.safe_alias(right)}',
            logic_group=logic_group
        )

    def _eval(self) -> Any:
        match self.op:
            case self.Operator.and_:
                return self.safe_eval(self.left) and self.safe_eval(self.right)
            case self.Operator.or_:
                return self.safe_eval(self.left) or self.safe_eval(self.right)
            case self.Operator.not_:
                return not self.safe_eval(self.left)
            case _:
                raise NotImplementedError(f'Invalid comparison operator {self.op}!')

    def _not(self) -> Any:
        res = not self.safe_eval(self.left)
        return res

    def to_json(self, fmt='dict') -> dict | str:
        json_dict = dict(
            expression_type=self.__class__.__name__,
            left=self.safe_dump(self.left),
            op=self.op.name,
            repr=self.repr
        )

        if self.right is not None:
            json_dict['right'] = self.safe_dump(self.right)

        if self.dtype is not None and self.dtype is not Any:
            json_dict['dtype'] = self.dtype_to_str(self.dtype)

        if fmt == 'dict':
            return json_dict
        else:
            return json.dumps(json_dict)

    @classmethod
    def from_json(cls, json_message: str | bytes | dict) -> Self:
        if isinstance(json_message, dict):
            json_dict = json_message
        else:
            json_dict = json.loads(json_message)

        assert json_dict['expression_type'] == cls.__name__

        kwargs = dict(
            left=cls.safe_load(json_dict['left']),
            op=cls.Operator[json_dict['op']],
            repr=json_dict['repr'],
        )

        if 'right' in json_dict:
            kwargs['right'] = cls.safe_load(json_dict['right'])

        if 'dtype' in json_dict:
            kwargs['dtype'] = cls.str_to_dtype(json_dict['dtype'])

        self = cls(
            **kwargs
        )

        return self
