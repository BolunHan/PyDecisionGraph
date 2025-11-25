from __future__ import annotations

import enum
import threading
import time
import uuid
from math import inf
from typing import Literal, Any

from algo_engine.profile import PROFILE

from . import LOGGER
from .base import SignalLogicGroup
from ..decision_tree import ActionNode, LogicGroup, NodeValueError, LongAction, ShortAction, LGM, NoAction

LOGGER = LOGGER.getChild('Request')

__all__ = [
    'RequestType', 'RequestStatus', 'PendingRequest',
    'RequestRegistered', 'RequestDenied', 'RequestConfirmed',
    'DelayedConfirmationLogicGroup'
]


class RequestType(enum.StrEnum):
    generic = enum.auto()
    open = enum.auto()
    unwind = enum.auto()


class RequestStatus(enum.StrEnum):
    active = enum.auto()
    idle = enum.auto()
    locked = enum.auto()


class PendingRequest(object):
    def __init__(
            self,
            name: str = None,
            uid: uuid.UUID = None,
            **kwargs
    ):
        self.uid = uid or uuid.uuid4()
        self.name = name or f'PendingRequest.{self.uid}'
        self.logic_group = kwargs.get('logic_group', LGM.active_group)

        self.state = {
            'sig': kwargs.get('sig', 0),
            'rtype': kwargs.get('rtype', RequestType.generic),
            'rstatus': kwargs.get('rstatus', RequestStatus.idle),
            'activated_ts': kwargs.get('activated_ts', 0),
            'activated_by': kwargs.get('activated_by'),
            'deactivated_by': kwargs.get('deactivated_by'),
            'confirmed_by': kwargs.get('confirmed_by'),
            'timeout': kwargs.get('timeout', inf),
        }

        self.register_node = {}
        self.confirmation_node = {}
        self.denial_node = {}
        self.lock = threading.Lock()

    def bind(self, node_id: uuid.UUID, logic_node: RequestRegistered | RequestDenied | RequestConfirmed):
        if isinstance(logic_node, RequestRegistered):
            self.register_node[node_id] = logic_node
        elif isinstance(logic_node, RequestDenied):
            self.denial_node[node_id] = logic_node
        elif isinstance(logic_node, RequestConfirmed):
            self.confirmation_node[node_id] = logic_node
        else:
            raise TypeError('PendingRequest can only be bound to RequestRegistered, RequestConfirmed, or RequestDenied nodes!')
        return logic_node

    def activate(self, node_id: uuid.UUID):
        if self.rstatus == RequestStatus.active:
            return

        if node_id not in self.register_node:
            raise NodeValueError('The provided node_id is not bound with this PendingRequest!')

        reg_node = self.register_node[node_id]

        if not reg_node.sig:
            raise NodeValueError('Signal Must not be zero.')

        if not self.lock.acquire(blocking=False):
            raise NodeValueError('Cannot activate a locked PendingRequest!')

        self.state['sig'] = reg_node.sig
        self.state['rtype'] = reg_node.rtype
        self.state['rstatus'] = RequestStatus.active
        self.state['activated_ts'] = self.logic_group.contexts.get('timestamp', time.time())
        self.state['activated_by'] = reg_node
        self.state['deactivated_by'] = None
        self.state['confirmed_by'] = None
        self.state['timeout'] = reg_node.timeout

        self.lock.release()

    def deactivate(self, node_id: uuid.UUID):
        if self.rstatus == RequestStatus.idle:
            return

        if node_id not in self.denial_node:
            raise NodeValueError('The provided node_id is not bound with this PendingRequest!')

        node = self.denial_node[node_id]

        if not self.lock.acquire(blocking=False):
            raise NodeValueError('Cannot deactivate a locked PendingRequest!')

        self.state['sig'] = 0
        self.state['rtype'] = RequestType.generic
        self.state['rstatus'] = RequestStatus.idle
        self.state['activated_ts'] = 0
        self.state['activated_by'] = None
        self.state['deactivated_by'] = node
        self.state['confirmed_by'] = None
        self.state['timeout'] = 0

        self.lock.release()

    def confirm(self, node_id: uuid.UUID) -> ShortAction | NoAction | LongAction:
        if self.rstatus != RequestStatus.active:
            raise NodeValueError('Cannot confirm a inactive PendingRequest!')

        if node_id not in self.confirmation_node:
            raise NodeValueError('The provided node_id is not bound with this PendingRequest!')

        node = self.confirmation_node[node_id]

        if not self.lock.acquire(blocking=False):
            raise NodeValueError('Cannot confirm a locked PendingRequest!')

        # Step 1: Lock the request
        self.state['rstatus'] = RequestStatus.locked
        prelaunch_check = True

        # Step 2: Check timeout
        current_ts = self.logic_group.contexts.get('timestamp', time.time())
        activated_ts = self.state['activated_ts']
        timeout = self.state['timeout']
        elapsed_time = PROFILE.trading_time_between(activated_ts, current_ts)
        if elapsed_time >= timeout:
            prelaunch_check = False

        # Step 3: Check signal match
        if prelaunch_check and node.sig != self.sig:
            prelaunch_check = False

        # Step 4: Finalize confirmation or denial
        if prelaunch_check:
            if node.sig > 0:
                action = LongAction(sig=node.sig, auto_connect=False)
            elif node.sig < 0:
                action = ShortAction(sig=node.sig, auto_connect=False)
            else:
                action = NoAction(auto_connect=False)
        else:
            action = NoAction(auto_connect=False)

        # Step 5: Reset the request state
        self.state['sig'] = 0
        self.state['rtype'] = RequestType.generic
        self.state['rstatus'] = RequestStatus.idle
        self.state['activated_ts'] = 0
        self.state['activated_by'] = None
        self.state['deactivated_by'] = None
        self.state['confirmed_by'] = node
        self.state['timeout'] = 0

        # Step 6: Release the lock and return the action
        self.lock.release()
        return action

    def __bool__(self):
        if not self.sig:
            return False
        return True

    @property
    def sig(self) -> int:
        return self.state['sig']

    @property
    def rtype(self) -> RequestType:
        return self.state['rtype']

    @property
    def rstatus(self) -> RequestStatus:
        return self.state['rstatus']

    @property
    def activated_ts(self) -> float:
        return self.state['activated_ts']

    @property
    def activated_by(self) -> RequestRegistered:
        return self.state['activated_by']

    @property
    def deactivated_by(self) -> RequestDenied:
        return self.state['deactivated_by']

    @property
    def confirmed_by(self) -> RequestConfirmed:
        return self.state['confirmed_by']

    @property
    def timeout(self) -> float:
        return self.state['timeout']


class RequestRegistered(ActionNode):
    def __new__(
            cls,
            *,
            sig: Literal[-1, 1],
            req: PendingRequest,
            rtype: RequestType = RequestType.generic,
            timeout: float = float('inf'),
            action=None,
            expression=None,
            dtype=None,
            repr=None,
            auto_connect: bool = True,
            **kwargs
    ):
        uid = uuid.uuid4()

        if action is None:
            action = lambda: req.activate(uid)

        if expression is None:
            expression = NoAction(auto_connect=False)

        instance = super().__new__(
            cls,
            action=action,
            expression=expression,
            dtype=dtype,
            repr=repr or f'<Pending Request Registered {sig=}>',
            auto_connect=auto_connect
        )

        instance.uid = uid
        instance.sig = sig
        instance.req = req
        instance.rtype = rtype
        instance.timeout = timeout

        return req.bind(uid, instance)


class RequestDenied(ActionNode):
    def __new__(
            cls,
            *,
            req: PendingRequest,
            action=None,
            expression=None,
            dtype=None,
            repr=None,
            auto_connect: bool = True,
            **kwargs
    ):
        uid = uuid.uuid4()

        if action is None:
            action = lambda: req.deactivate(uid)

        if expression is None:
            expression = NoAction(auto_connect=False)

        instance = super().__new__(
            cls,
            action=action,
            expression=expression,
            dtype=dtype,
            repr=repr or '<Pending Request Denied>',
            auto_connect=auto_connect
        )

        instance.uid = uid
        instance.req = req
        return req.bind(uid, instance)


class RequestConfirmed(ActionNode):
    def __new__(
            cls,
            *,
            sig: Literal[-1, 1],
            req: PendingRequest,
            action=None,
            expression=None,
            dtype=None,
            repr=None,
            auto_connect: bool = True,
            **kwargs
    ):
        uid = uuid.uuid4()

        if action is None:
            action = lambda: req.confirm(uid)

        if expression is None:
            expression = lambda: req.confirm(uid)

        instance = super().__new__(
            cls,
            action=action,
            expression=expression,
            dtype=dtype,
            repr=repr or f'<Pending Request Confirmed {sig=}>',
            auto_connect=auto_connect
        )

        instance.uid = uid
        instance.sig = sig
        instance.req = req
        return req.bind(uid, instance)


class DelayedConfirmationLogicGroup(SignalLogicGroup):
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

        instance.req = instance.contexts['pending_request'] = PendingRequest()
        return instance

    def register(self, sig: Literal[1, -1], timeout: float = float('inf'), rtype: RequestType = RequestType.generic) -> RequestRegistered:
        action_register = RequestRegistered(
            sig=sig,
            req=self.req,
            rtype=rtype,
            timeout=timeout
        )
        return action_register

    def deny(self) -> RequestDenied:
        action_deny = RequestDenied(
            req=self.req
        )
        return action_deny

    def confirm(self, sig: Literal[1, -1]) -> RequestConfirmed:
        action_confirm = RequestConfirmed(
            sig=sig,
            req=self.req
        )
        return action_confirm

    def reset(self):
        self.req.reset()
        super().reset()

    @property
    def signal(self):
        return self.parent.signal

    @signal.setter
    def signal(self, value: Literal[-1, 0, 1]):
        assert isinstance(value, (int, float))
        self.parent.signal = value
