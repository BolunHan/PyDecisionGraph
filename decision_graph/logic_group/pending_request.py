from __future__ import annotations

import enum
import time
import uuid
from collections.abc import Mapping
from typing import Literal, Self, TypedDict

from . import LOGGER
from .base import SignalLogicGroup
from ..decision_tree import ActionNode, LogicMapping, LogicGroup, NodeValueError, LongAction, ShortAction, LGM, NoAction

LOGGER = LOGGER.getChild('request')

__all__ = ['RequestAction', 'PendingRequest', 'RequestConfirmed', 'RequestDenied', 'RequestRegistered', 'DelayedConfirmationLogicGroup']


class StateMapping(TypedDict):
    timestamp: float


class RequestAction(enum.StrEnum):
    open = enum.auto()
    unwind = enum.auto()
    idle = enum.auto()


class PendingRequest(dict):

    def __init__(
            self,
            name: str,
            timestamp: float,
            sig: Literal[-1, 1] | int,
            action: str,
            timeout: float,
            logic_group: LogicGroup = None,
            uid: uuid.UUID = None,
            **kwargs
    ):
        super().__init__(
            name=name,
            timestamp=timestamp,
            sig=sig,
            timeout=timeout,
            action=RequestAction(action),
            uid=uuid.uuid4() if uid is None else uid,
            **kwargs
        )

        self.logic_group = logic_group

    def reset(self) -> PendingRequest:
        self.update(
            name='DummyRequest',
            timestamp=0,
            sig=0,
            action=RequestAction.idle,
            timeout=0,
            uid=uuid.UUID(int=0)
        )
        return self

    @classmethod
    def empty(cls) -> PendingRequest:
        return PendingRequest(
            name='DummyRequest',
            timestamp=0,
            sig=0,
            action=RequestAction.idle,
            timeout=0,
            uid=uuid.UUID(int=0)
        )

    def __bool__(self):
        if not self.sig:
            return False
        return True

    @property
    def name(self) -> str:
        return self['name']

    @property
    def timestamp(self) -> float:
        return self['timestamp']

    @property
    def sig(self) -> int:
        return self['sig']

    @property
    def timeout(self) -> float:
        return self['timeout']

    @property
    def action(self) -> RequestAction:
        return self['action']

    @property
    def uid(self) -> uuid.UUID:
        return self['uid']


class RequestConfirmed(ActionNode):
    def __init__(self, sig: Literal[-1, 1], req: PendingRequest, auto_connect: bool = True):
        super().__init__(
            repr=f'<Pending Request Confirmed {sig=}>',
            auto_connect=auto_connect
        )

        self.sig = sig
        self.req = req

    def eval(self, enforce_dtype: bool = False) -> ActionNode:
        sig = self.sig

        if sig > 0:
            return LongAction(sig=sig, auto_connect=False)
        elif sig < 0:
            return ShortAction(sig=sig, auto_connect=False)

        if not LGM.inspection_mode:
            LOGGER.warning(f'{self} received a confirmation of {sig=}! Which is not expected.')

        return NoAction(auto_connect=False)

    def _post_eval(self) -> Self:
        self.req.reset()


class RequestDenied(ActionNode):
    def __init__(self, req: PendingRequest, auto_connect: bool = True):
        super().__init__(
            repr='<Pending Request Denied>',
            auto_connect=auto_connect
        )

        self.req = req

    def eval(self, enforce_dtype: bool = False) -> ActionNode:
        return NoAction(auto_connect=False)

    def _post_eval(self) -> Self:
        self.req.reset()


class RequestRegistered(ActionNode):
    def __init__(self, sig: Literal[-1, 1], req: PendingRequest, state: StateMapping | Mapping | LogicMapping = None, action: RequestAction = RequestAction.open, timeout: float = float('inf'), auto_connect: bool = True):
        super().__init__(
            action=self._registered,
            repr=f'<Pending Request Registered {sig=}>',
            auto_connect=auto_connect
        )

        self.state = state
        self.req = req

        self.sig = sig
        self.action = action
        self.timeout = timeout

    def eval(self, enforce_dtype: bool = False) -> ActionNode:
        return NoAction(auto_connect=False)

    def _registered(self) -> Self:
        sig = self.sig
        timestamp = time.time() if self.state is None else self.state['timestamp']
        action = self.action
        timeout = self.timeout
        uid = uuid.uuid4()

        if self.sig > 0:
            name = 'state.long'
        elif self.sig < 0:
            name = 'state.short'
        else:
            raise NodeValueError('Signal Must not be zero.')

        self.req.update(
            name=name,
            timestamp=timestamp,
            sig=sig,
            timeout=timeout,
            action=action,
            uid=uid
        )

        return self


class DelayedConfirmationLogicGroup(SignalLogicGroup):
    def __init__(self, parent: SignalLogicGroup, name: str = None):
        super().__init__(
            name=f'{parent.name}.Delayed' if name is None else name,
            parent=parent
        )

        self.req = self.contexts['pending_request'] = PendingRequest.empty()

    def register(self, sig: Literal[1, -1], state: StateMapping | Mapping | LogicMapping = None, timeout: float = float('inf'), action: RequestAction | str = RequestAction.open) -> RequestRegistered:
        action_register = RequestRegistered(
            sig=sig,
            req=self.req,
            state=state,
            action=RequestAction(action),
            timeout=timeout
        )

        return action_register

    def confirm(self, sig: Literal[1, -1]) -> RequestConfirmed:
        action_confirm = RequestConfirmed(req=self.req, sig=sig)
        return action_confirm

    def deny(self) -> RequestDenied:
        action_deny = RequestDenied(req=self.req)
        return action_deny

    def reset(self):
        self.pending_request.reset()
        super().reset()

    @property
    def action(self) -> RequestAction:
        return self.pending_request.action

    @property
    def pending_request(self) -> LogicMapping:
        req = self.req

        m = LogicMapping(
            data=req,
            name=f'{self.name}.PendingRequest',
            logic_group=self
        )

        return m

    @pending_request.setter
    def pending_request(self, value: PendingRequest):
        LOGGER.warning('Assigning pending request will break the reference of previous generated decision graph. Use with caution.')
        assert isinstance(value, PendingRequest)
        self.req = self.contexts['pending_request'] = value

    @property
    def signal(self):
        return self.parent.signal

    @signal.setter
    def signal(self, value: Literal[-1, 0, 1]):
        assert isinstance(value, (int, float))
        self.parent.signal = value
