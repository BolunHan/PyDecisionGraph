from __future__ import annotations

from collections.abc import Mapping, Sequence
from typing import Any

from . import AttrExpression
from .abc import LogicGroup, ExpressionCollection

__all__ = ['LogicMapping', 'LogicGenerator']


class LogicMapping(ExpressionCollection):
    def __init__(self, data: dict, name: str, logic_group: LogicGroup = None):
        if data is None:
            data = {}

        if not isinstance(data, Mapping):
            raise TypeError("The 'data' parameter must be a mapping!.")

        super().__init__(
            data=data,
            name=name,
            logic_group=logic_group
        )

    def __bool__(self):
        return bool(self.data)

    def __len__(self):
        return self.data.__len__()

    def __getitem__(self, key: str):
        return AttrExpression(attr=key, logic_group=self)

    def __getattr__(self, key: str):
        return AttrExpression(attr=key, logic_group=self)

    def reset(self):
        pass

    def update(self, data: dict = None, **kwargs):
        if data is None:
            self.data.update(**kwargs)
        else:
            self.data.update(data, **kwargs)

    def clear(self):
        self.data.clear()


class LogicGenerator(ExpressionCollection):
    def __init__(self, data: list[Any], name: str, logic_group: LogicGroup = None):
        if data is None:
            data = []

        if not isinstance(data, Sequence):
            raise TypeError("The 'data' parameter must be a sequence!.")

        super().__init__(
            data=data,
            name=name,
            logic_group=logic_group
        )

        self.data = self.contexts.setdefault('data', data)

    def __iter__(self):
        for index, value in enumerate(self.data):
            # if isinstance(value, ContextLogicExpression):
            #     yield value

            yield AttrExpression(attr=index, logic_group=self)

    def __len__(self) -> int:
        return len(self.data)

    def __getitem__(self, index: int):
        return AttrExpression(attr=index, logic_group=self)

    def append(self, value):
        self.data.append(value)

    def remove(self, value):
        self.data.remove(value)

    def clear(self):
        return self.data.clear()
