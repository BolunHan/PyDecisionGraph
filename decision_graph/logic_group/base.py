from __future__ import annotations

from typing import Literal, Any, overload, final

from . import LOGGER
from ..decision_tree import USING_CAPI, AttrExpression, LogicGroup, ActionNode, LGM, LongAction, ShortAction, NoAction

LOGGER = LOGGER.getChild('base')

__all__ = ['SignalLogicGroup', 'InstantConfirmationLogicGroup']


class SignalLogicGroup(LogicGroup):
    def __new__(
            cls,
            *,
            name: str = None,
            parent: LogicGroup = None,
            contexts: dict[str, Any] = None,
            **kwargs
    ):
        instance = super().__new__(
            cls,
            name=name,
            parent=parent,
            contexts=contexts
        )
        instance.signal = 0
        return instance

    def get(self, attr: str, dtype: type = None, repr: str = None):
        return AttrExpression(attr=attr, logic_group=self, dtype=dtype, repr=repr)

    def reset(self):
        self.signal = 0

    @property
    def signal(self):
        return self.contexts.setdefault('signal', 0)

    @signal.setter
    def signal(self, value: int):
        self.contexts['signal'] = value


@final
class InstantConfirmationLogicGroup(SignalLogicGroup):
    def __new__(
            cls,
            *,
            name: str = None,
            parent: LogicGroup = None,
            contexts: dict[str, Any] = None,
            **kwargs
    ):
        if not parent:
            parent = LGM.active_group

        if not isinstance(parent, SignalLogicGroup):
            raise TypeError('InstantConfirmationLogicGroup requires a SignalLogicGroup as parent!')

        instance = super().__new__(
            cls,
            name=f'{parent.name}.Instant' if not name else name,
            parent=parent,
            contexts=contexts
        )
        return instance

    def reset(self):
        pass

    @overload
    def confirm(self, sig: Literal[1]) -> LongAction:
        ...

    @overload
    def confirm(self, sig: Literal[-1]) -> ShortAction:
        ...

    def confirm(self, sig: Literal[-1, 1]) -> ActionNode:
        self.signal = sig

        if sig > 0:
            return LongAction(sig=sig)
        elif sig < 0:
            return ShortAction(sig=sig)

        if not LGM.inspection_mode:
            LOGGER.warning(f'{self} received a confirmation of {sig=}! Which is not expected.')

        return NoAction()

    @property
    def signal(self):
        return self.parent.signal

    @signal.setter
    def signal(self, value: int):
        self.parent.signal = value
