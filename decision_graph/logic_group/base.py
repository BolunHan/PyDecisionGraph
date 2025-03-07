from __future__ import annotations

from typing import Literal, Any, Self, overload

from . import LOGGER
from ..decision_tree import AttrExpression, LogicGroup, ActionNode, LGM, LongAction, ShortAction, NoAction

LOGGER = LOGGER.getChild('base')

__all__ = ['SignalLogicGroup', 'InstantConfirmationLogicGroup']


class SignalLogicGroup(LogicGroup):
    def __init__(self, name: str, parent: Self = None, contexts: dict[str, Any] = None):
        super().__init__(name=name, parent=parent, contexts=contexts)

    def get(self, attr: str, dtype: type = None, repr: str = None):
        """
        Retrieve an attribute as a LogicExpression.
        """
        return AttrExpression(attr=attr, logic_group=self, dtype=dtype, repr=repr)

    def reset(self):
        self.signal = 0

    @property
    def signal(self):
        return self.contexts.get('signal', 0)

    @signal.setter
    def signal(self, value: int):
        self.contexts['signal'] = value


class InstantConfirmationLogicGroup(SignalLogicGroup):
    def __init__(self, parent: SignalLogicGroup, name: str = None):
        super().__init__(
            name=f'{parent.name}.Instant' if name is None else name,
            parent=parent
        )

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
