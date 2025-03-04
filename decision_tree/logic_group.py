from __future__ import annotations

import enum
import uuid
from typing import Literal, Any, Self

from . import AttrExpression, LogicMapping
from .abc import LogicGroup, SkipContextsBlock

__all__ = ['SignalLogicGroup', 'InstantConfirmationLogicGroup', 'RequestAction', 'PendingRequest', 'DelayedConfirmationLogicGroup', 'RacingConfirmationLogicGroup', 'BarrierConfirmationLogicGroup']


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

    def confirm(self, sig: Literal[-1, 1]):
        self.signal = sig
        return

    @property
    def signal(self):
        return self.parent.signal

    @signal.setter
    def signal(self, value: int):
        self.parent.signal = value


class RequestAction(enum.StrEnum):
    open = enum.auto()
    unwind = enum.auto()
    idle = enum.auto()


class PendingRequest(dict):
    class Skip(Exception):
        pass

    def __init__(
            self,
            name: str | RequestAction,
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


class DelayedConfirmationLogicGroup(SignalLogicGroup):
    def __init__(self, parent: SignalLogicGroup, name: str = None):
        super().__init__(
            name=f'{parent.name}.Delayed' if name is None else name,
            parent=parent
        )

    def register(self, name: str, timestamp: float, sig: Literal[1, -1], timeout: float | None, action: Literal['open', 'unwind'] = 'open', uid: uuid.UUID = None, **kwargs):
        req = self.pending_request = PendingRequest(
            name=name,
            logic_group=self,
            timestamp=timestamp,
            sig=sig,
            timeout=timeout,
            action=action,
            uid=uid,
            **kwargs
        )

        return req

    def confirm(self):
        req = self.contexts.get('pending_request')
        sig = 0 if req is None else req.sig
        self.reset()
        self.signal = sig
        return sig

    def deny(self):
        # denying all the pending request
        self.reset()
        return 0

    def reset(self):
        # self.pending_request = PendingRequest.empty()
        self.contexts.pop('pending_request', None)
        super().reset()

    @property
    def action(self) -> RequestAction:
        if 'pending_request' in self.contexts:
            return self.pending_request.action

        return RequestAction.idle

    @property
    def pending_request(self) -> LogicMapping | SkipContextsBlock:

        if (req := self.contexts.get('pending_request')) is None:
            return SkipContextsBlock(True)

        m = LogicMapping(
            data=req,
            name=f'{self.name}.PendingRequest.{req.uid.hex}',
            logic_group=self
        )

        return m

    @pending_request.setter
    def pending_request(self, value: PendingRequest):
        assert isinstance(value, PendingRequest)
        self.contexts['pending_request'] = value

    @property
    def signal(self):
        return self.parent.signal

    @signal.setter
    def signal(self, value: Literal[-1, 0, 1]):
        assert isinstance(value, (int, float))
        self.parent.signal = value


class RacingConfirmationLogicGroup(DelayedConfirmationLogicGroup):

    def __init__(self, parent: SignalLogicGroup, name: str = None):
        super().__init__(
            name=f'{parent.name}.Racing' if name is None else name,
            parent=parent
        )

    def __getitem__(self, uid: uuid.UUID | str | bytes | int) -> PendingRequest:
        if not (request_pool := self.pending_request):
            raise KeyError(f'uid {uid} not found!')

        match uid:
            case uuid.UUID():
                for _pending_request in request_pool:
                    if _pending_request.uid == uid:
                        return _pending_request
                raise KeyError(f'uid {uid} not found!')
            case str():
                for _pending_request in request_pool:
                    if _pending_request.uid.hex == uid:
                        return _pending_request
                raise KeyError(f'uid {uid} not found!')
            case bytes():
                for _pending_request in request_pool:
                    if _pending_request.uid.bytes == uid:
                        return _pending_request
                raise KeyError(f'uid {uid} not found!')
            case int():
                return request_pool[uid]
            case _:
                raise TypeError(f'Invalid uid {uid}! Expected UUID or bytes or str!')

    def register(self, name: str, timestamp: float, sig: Literal[1, -1], timeout: float, action: Literal['open', 'unwind'] = 'open', uid: uuid.UUID = None, **kwargs):
        self.pending_request.append(
            PendingRequest(
                name=name,
                timestamp=timestamp,
                sig=sig,
                timeout=timeout,
                action=action,
                uid=uid,
                **kwargs
            )
        )

    def confirm(self, pending_request: PendingRequest = None, uid: uuid.UUID = None):
        if pending_request is None and uid is None:
            assert len(self.pending_request) == 1, ValueError('Multiple pending requests found! Must assign uid or pending_request instance!')
            pending_request = self.pending_request[0]
        elif pending_request is None:
            pending_request = self.__getitem__(uid=uid)

        sig = pending_request.sig
        self.reset()
        self.signal = sig
        return sig

    def deny(self, pending_request: PendingRequest = None, uid: uuid.UUID = None):
        # denying all the pending request
        if pending_request is None and uid is None:
            self.pending_request.clear()
            self.signal = 0
            return

        if pending_request is not None:
            self.pending_request.remove(pending_request)
            self.signal = 0

        if uid is not None:
            pending_request = self.__getitem__(uid=uid)
            self.pending_request.remove(pending_request)
            self.signal = 0

    @property
    def pending_request(self) -> list[PendingRequest]:
        return self.contexts.setdefault('pending_request', [])


class BarrierConfirmationLogicGroup(RacingConfirmationLogicGroup):

    def __init__(self, parent: SignalLogicGroup, name: str = None):
        super().__init__(
            name=f'{parent.name}.Barrier' if name is None else name,
            parent=parent
        )

    def confirm(self, pending_request: PendingRequest = None, uid: uuid.UUID = None):
        if pending_request is None and uid is None:
            assert len(self.pending_request) == 1, ValueError('Multiple pending requests found! Must assign uid or pending_request instance!')
            pending_request = self.pending_request[0]
        elif pending_request is None:
            pending_request = self.__getitem__(uid=uid)

        self.pending_request.remove(pending_request)

        if self.pending_request:
            return 0

        sig = pending_request.sig
        self.signal = sig
        return sig
