from collections.abc import Mapping, Sequence, Generator

from . import LOGGER
from .abc import LogicGroup


class LogicMapping(LogicGroup):
    def __init__(self, *, name: str, data: dict | None = None, parent: LogicGroup | None = None, contexts: dict | None = None, **kwargs):
        super().__init__(name=name, parent=parent, contexts=contexts, **kwargs)
        if data is None:
            ctx_data = self.contexts.setdefault('data', {})
            if isinstance(ctx_data, dict):
                self.data = ctx_data
            elif isinstance(ctx_data, Mapping):
                LOGGER.info(f'Using non-dict mapping for {self} data, unlinking and converting to dict...')
                self.data = dict(ctx_data)
            else:
                raise TypeError("The 'data' parameter must be a Mapping!")
        else:
            self.data = data

    def _get(self, key: str):
        return self.data[key]

    def __bool__(self) -> bool:
        return bool(self.data)

    def __len__(self) -> int:
        return len(self.data)

    def __getitem__(self, key: str):
        from .node import AttrExpression
        return AttrExpression(attr=key, logic_group=self)

    def __getattr__(self, key: str):
        from .node import AttrExpression
        return AttrExpression(attr=key, logic_group=self)

    def __contains__(self, key: str) -> bool:
        return key in self.data

    def update(self, *args, **kwargs) -> None:
        self.data.update(*args, **kwargs)

    def clear(self) -> None:
        self.data.clear()


class LogicSequence(LogicGroup):
    def __init__(self, *, name: str=None, data: list | None = None, parent: LogicGroup | None = None, contexts: dict | None = None, **kwargs):
        super().__init__(name=name, parent=parent, contexts=contexts, **kwargs)
        if data is None:
            ctx_data = self.contexts.setdefault('data', [])
            if isinstance(ctx_data, list):
                self.data = ctx_data
            elif isinstance(ctx_data, Sequence) and not isinstance(ctx_data, (str, bytes)):
                LOGGER.info(f'Using non-list sequence for {self} data, converting to list...')
                self.data = list(ctx_data)
            else:
                raise TypeError("The 'data' parameter must be a Sequence!")
        else:
            self.data = data

    def _get(self, index: int):
        return self.data[index]

    def __iter__(self):
        from .node import AttrExpression
        for index in range(len(self.data)):
            yield AttrExpression(attr=index, logic_group=self)

    def __len__(self) -> int:
        return len(self.data)

    def __getitem__(self, index: int):
        from .node import AttrExpression
        return AttrExpression(attr=index, logic_group=self)

    def __contains__(self, item) -> bool:
        return item in self.data

    def append(self, value) -> None:
        self.data.append(value)

    def extend(self, iterable) -> None:
        self.data.extend(iterable)

    def insert(self, index: int, value) -> None:
        self.data.insert(index, value)

    def remove(self, value) -> None:
        self.data.remove(value)

    def pop(self, index: int = -1):
        return self.data.pop(index)

    def clear(self) -> None:
        self.data.clear()

    def __bool__(self) -> bool:
        return bool(self.data)


class LogicGenerator(LogicGroup):
    def __init__(self, *, name: str, data: Generator | None = None, parent: LogicGroup | None = None, contexts: dict | None = None, **kwargs):
        super().__init__(name=name, parent=parent, contexts=contexts, **kwargs)
        if data is None:
            data = self.contexts.get('data')

        if isinstance(data, Generator):
            self.data = data
        else:
            raise TypeError("The 'data' parameter must be a Generator!")

    def _next(self):
        # Simulates c_next
        return next(self.data)

    # === Python generator protocol ===

    def __iter__(self):
        return self

    def __next__(self):
        return self._next()

    def send(self, value):
        return self.data.send(value)

    def throw(self, typ, val=None, tb=None):
        return self.data.throw(typ, val, tb)

    def close(self):
        return self.data.close()
