from __future__ import annotations

import linecache
import operator
import sys
import uuid
from collections.abc import Callable
from typing import Any, Self, final

from . import LOGGER
from ..exc import *

LOGGER = LOGGER.getChild('abc')

__all__ = ['Singleton',
           'NodeEdgeCondition', 'ConditionElse', 'ConditionAny', 'ConditionAuto', 'BinaryCondition', 'ConditionTrue', 'ConditionFalse',
           'NO_CONDITION', 'ELSE_CONDITION', 'AUTO_CONDITION', 'TRUE_CONDITION', 'FALSE_CONDITION',
           'SkipContextsBlock', 'LogicExpression', 'LogicNode',
           'LogicGroupManager', 'LGM', 'LogicGroup',
           'ActionNode', 'BreakpointNode', 'PlaceholderNode',
           'NoAction', 'LongAction', 'ShortAction', 'CancelAction']


class Singleton(type):
    _instances = {}

    def __call__(cls, *args, **kwargs):
        if cls not in cls._instances:
            cls._instances[cls] = super(Singleton, cls).__call__(*args, **kwargs)
        return cls._instances[cls]


class NodeEdgeCondition(metaclass=Singleton):
    def __init__(self, value=None):
        self._value = value

    def __hash__(self):
        return hash(self._value)

    def __repr__(self):
        return f'<{self.__class__.__name__} {id(self):#0x}>'

    @property
    def value(self):
        if self._value is None:
            raise ValueError("Condition has no value assigned.")
        return self._value

    @value.setter
    def value(self, value):
        self._value = value


class ConditionElse(NodeEdgeCondition):
    def __init__(self, *args, **kwargs):
        super().__init__()
        self._value = None

    def __hash__(self):
        return id(self)

    def __repr__(self):
        return f'<CONDITION Internal {id(self):#0x}>(Else)'

    def __str__(self):
        return 'Else'

    @NodeEdgeCondition.value.setter
    def value(self, value):
        raise NotImplementedError()


class ConditionAny(NodeEdgeCondition):
    def __init__(self, *args, **kwargs):
        super().__init__()
        self._value = None

    def __hash__(self):
        return id(self)

    def __repr__(self):
        return f'<CONDITION {id(self):#0x}>(Unconditional)'

    def __str__(self):
        return 'Unconditional'

    @NodeEdgeCondition.value.setter
    def value(self, value):
        raise NotImplementedError()


class ConditionAuto(NodeEdgeCondition):
    def __init__(self, *args, **kwargs):
        super().__init__()
        self._value = None

    def __hash__(self):
        raise NotImplementedError()

    def __repr__(self):
        return f'<CONDITION Internal {id(self):#0x}>(AutoInfer)'

    def __str__(self):
        return 'AutoInfer'


class BinaryCondition(NodeEdgeCondition):
    def __init__(self, *args, **kwargs):
        super().__init__()
        self._value = None

    def __hash__(self):
        return id(self)

    def __invert__(self):
        raise NotImplementedError()

    @NodeEdgeCondition.value.setter
    def value(self, value):
        raise NotImplementedError()


class ConditionTrue(BinaryCondition):
    def __repr__(self):
        return f'<CONDITION {id(self):#0x}>(True)'

    def __str__(self):
        return 'True'

    def __bool__(self):
        return True

    def __int__(self):
        return 1

    def __neg__(self):
        return FALSE_CONDITION

    def __invert__(self):
        return FALSE_CONDITION

    @property
    def value(self):
        return True


class ConditionFalse(BinaryCondition):
    def __repr__(self):
        return f'<CONDITION {id(self):#0x}>(False)'

    def __str__(self):
        return 'False'

    def __bool__(self):
        return False

    def __int__(self):
        return 0

    def __neg__(self):
        return TRUE_CONDITION

    def __invert__(self):
        return TRUE_CONDITION

    @property
    def value(self):
        return False


NO_CONDITION = ConditionAny()
ELSE_CONDITION = ConditionElse()
AUTO_CONDITION = ConditionAuto()
TRUE_CONDITION = ConditionTrue()
FALSE_CONDITION = ConditionFalse()


class SkipContextsBlock(object):
    def __init__(self):
        self.skip_exception = type(f"{self.__class__.__name__}SkipException", (EmptyBlock,), {"owner": self})
        self.tracer_override = False
        self.default_entry_check = True
        self.__cframe = None
        self.__original_trace = None
        self.__enter_line = None

    def _entry_check(self) -> Any:
        return self.default_entry_check

    @final
    def __enter__(self):
        if self._entry_check():  # Check if the expression evaluates to True
            self._on_enter()
            return self

        self.__cframe = sys._getframe().f_back
        self.__original_trace = self.get_trace()
        self.__enter_line = (self.__cframe.f_code.co_filename, self.__cframe.f_lineno)
        self.__cframe.f_trace = self.__tracer_skipper
        sys.settrace(self.__tracer_skipper)
        self.tracer_override = True

    @final
    def __exit__(self, exc_type, exc_value, exc_traceback):
        sys.settrace(None)
        self.__restore_trace()

        if exc_type is None:
            self._on_exit()
            return

        if issubclass(exc_type, self.skip_exception):
            return True

        self._on_exit()
        # Propagate any other exception raised in the block
        return False

    def _on_enter(self):
        pass

    def _on_exit(self):
        pass

    @staticmethod
    def get_trace():
        try:
            # Check if PyDev debugger is active
            # noinspection PyUnresolvedReferences
            import pydevd
            debugger = pydevd.GetGlobalDebugger()
            if debugger is not None:
                return debugger.trace_dispatch  # Use PyDev's trace function
        except ImportError:
            pass  # PyDev debugger is not installed or active

        # Fall back to the standard trace function
        return sys.gettrace()

    def __restore_trace(self):
        if self.tracer_override:
            # print('[restore_trace]', f'storing tracer to {self.__original_trace}.')
            self.__cframe.f_trace = self.__original_trace
            sys.settrace(self.__original_trace)  # Restore the original trace

    def __tracer_skipper(self, frame, event, arg):
        line = linecache.getline(frame.f_code.co_filename, frame.f_lineno).strip()
        # print(f'[SkipContextsBlock]', line, frame, event, arg, (frame.f_code.co_filename, frame.f_lineno), self.__enter_line)
        if line.startswith(('pass', '...')):
            return self.__tracer_skipper
        elif self.__enter_line == (frame.f_code.co_filename, frame.f_lineno):
            # print(f'[SkipContextsBlock]', 'Restoring trace...')
            self.__restore_trace()
            return self.__tracer_skipper
        elif self.tracer_override:
            raise self.skip_exception("Expression evaluated to be False, cannot enter the block.")


class LogicExpression(SkipContextsBlock):
    def __init__(self, *, expression: float | int | bool | Exception | Callable[[], Any], dtype: type = None, repr: str = None, uid: uuid.UUID = None):
        super().__init__()
        self.expression = expression
        self.dtype = dtype
        self.repr = repr if repr is not None else str(expression)
        self.uid = uuid.uuid4() if uid is None else uid

    def _entry_check(self) -> Any:
        return bool(self._eval(False))

    def eval(self, enforce_dtype: bool = False) -> Any:
        return self._eval(enforce_dtype)

    def _eval(self, enforce_dtype: bool) -> Any:
        if isinstance(self.expression, (float, int, bool, str)):
            value = self.expression
        elif callable(self.expression):
            value = self.expression()
        elif isinstance(self.expression, Exception):
            raise self.expression
        else:
            raise TypeError(f"Unsupported expression type: {type(self.expression)}.")

        if self.dtype is Any or self.dtype is None:
            pass  # No type enforcement
        elif enforce_dtype:
            value = self.dtype(value)
        elif not isinstance(value, self.dtype):
            LOGGER.warning(f"Evaluated value {value} does not match dtype {self.dtype.__name__}.")

        return value

    @classmethod
    def cast(cls, value: int | float | bool | Exception | Self, dtype: type = None) -> Self:
        if isinstance(value, LogicExpression):
            return value
        if isinstance(value, (int, float, bool)):
            return LogicExpression(
                expression=value,
                dtype=dtype or type(value),
                repr=str(value)
            )
        if callable(value):
            return LogicExpression(
                expression=value,
                dtype=dtype or Any,
                repr=f"Eval({value})"
            )
        if isinstance(value, Exception):
            return LogicExpression(
                expression=value,
                dtype=dtype or Any,
                repr=f"Raises({type(value).__name__}: {value})"
            )
        raise TypeError(f"Unsupported type for LogicExpression conversion: {type(value)}.")

    def __bool__(self) -> bool:
        return bool(self.eval())

    def __and__(self, other: Self | bool) -> Self:
        other_expr = self.cast(value=other, dtype=bool)
        new_expr = LogicExpression(
            expression=lambda: self.eval() and other_expr.eval(),
            dtype=bool,
            repr=f"({self.repr} and {other_expr.repr})"
        )
        return new_expr

    def __eq__(self, other: int | float | bool | str | Self) -> bool:
        """
        This behavior must differ from the CAPI version.
        Python interface of __eq__ must return a boolean value.
        Otherwise it interferes with the __contains__ operation.
        Comparing reference is the most safe way.
        """
        return id(self) == id(other)

    def __or__(self, other: Self | bool) -> Self:
        other_expr = self.cast(value=other, dtype=bool)
        new_expr = LogicExpression(
            expression=lambda: self.eval() or other_expr.eval(),
            dtype=bool,
            repr=f"({self.repr} or {other_expr.repr})"
        )
        return new_expr

    # Math operators
    @staticmethod
    def _math_op(self: LogicExpression, other: int | float | LogicExpression, op: Callable, operator_str: str, dtype: type = None) -> LogicExpression:
        other_expr = LogicExpression.cast(other)

        if dtype is None:
            dtype = self.dtype

        new_expr = LogicExpression(
            expression=lambda: op(self.eval(), other_expr.eval()),
            dtype=dtype,
            repr=f"({self.repr} {operator_str} {other_expr.repr})",
        )
        return new_expr

    def __add__(self, other: int | float | bool | Self) -> Self:
        return LogicExpression._math_op(self, other, operator.add, "+", None)

    def __sub__(self, other: int | float | bool | Self) -> Self:
        return LogicExpression._math_op(self, other, operator.sub, "-", None)

    def __mul__(self, other: int | float | bool | Self) -> Self:
        return LogicExpression._math_op(self, other, operator.mul, "*", None)

    def __truediv__(self, other: int | float | bool | Self) -> Self:
        return LogicExpression._math_op(self, other, operator.truediv, "/", None)

    def __floordiv__(self, other: int | float | bool | Self) -> Self:
        return LogicExpression._math_op(self, other, operator.floordiv, "//", None)

    def __pow__(self, other: int | float | bool | Self) -> Self:
        return LogicExpression._math_op(self, other, operator.pow, "**", None)

    # Comparison operators, note that __eq__, __ne__ is special and should not implement as math operator
    def __lt__(self, other: int | float | bool | Self) -> Self:
        return LogicExpression._math_op(self, other, operator.lt, "<", bool)

    def __le__(self, other: int | float | bool | Self) -> Self:
        return LogicExpression._math_op(self, other, operator.le, "<=", bool)

    def __gt__(self, other: int | float | bool | Self) -> Self:
        return LogicExpression._math_op(self, other, operator.gt, ">", bool)

    def __ge__(self, other: int | float | bool | Self) -> Self:
        return LogicExpression._math_op(self, other, operator.ge, ">=", bool)

    def __repr__(self) -> str:
        return f"<{self.__class__.__name__}>(dtype={'Any' if self.dtype is None else self.dtype.__name__}, repr={self.repr})"


class LogicGroupManager(metaclass=Singleton):
    def __init__(self):
        # Dictionary to store cached LogicGroup instances
        self._cache = {}

        # Stack cursors: top of stack is at index 0
        self._active_groups: list[LogicGroup] = []
        self._active_nodes: list[LogicNode] = []
        self._breakpoint_nodes: list[BreakpointNode] = []
        self._shelved_state: list[dict] = []

        self.inspection_mode = False
        self.vigilant_mode = False

    def __call__(self, name: str, cls: type[LogicGroup], **kwargs) -> LogicGroup:
        reg_key = (cls.__module__, cls.__qualname__)
        registry = self._cache.get(reg_key)
        if registry is None:
            registry = self._cache[reg_key] = {}

        if name in registry:
            return registry[name]

        logic_group = cls(name=name, **kwargs)
        registry[name] = logic_group
        return logic_group

    def __contains__(self, instance: LogicGroup) -> bool:
        cls = instance.__class__
        name = instance.name
        reg_key = (cls.__module__, cls.__qualname__)
        registry = self._cache.get(reg_key)
        if registry is None:
            return False
        return name in registry

    def _lg_enter(self, logic_group: LogicGroup):
        # Set parent if there's an active group
        if self._active_groups:
            logic_group.parent = self._active_groups[0]
        self._active_groups.insert(0, logic_group)

    def _lg_exit(self, logic_group: LogicGroup = None):
        if not self._active_groups:
            raise RuntimeError("No active LogicGroup to exit.")

        current = self._active_groups[0]
        if logic_group is not None and current is not logic_group:
            raise AssertionError("The LogicGroup is not currently active.")
        # If logic_group is None, we exit the top one (current)

        # Activate pending breakpoints tied to this group
        for node in self._breakpoint_nodes:
            if node.break_from is current:
                node.await_connection = True

        self._active_groups.pop(0)

    def _ln_enter(self, logic_node: LogicNode):
        if isinstance(logic_node, ActionNode):
            LOGGER.error('Enter the with code block of an ActionNode rejected. Check if this is intentional?')
            return

        # Connect and remove all awaiting breakpoint nodes
        for breakpoint_node in self._breakpoint_nodes[:]:
            if breakpoint_node.await_connection:
                breakpoint_node._connect(logic_node)
                self._breakpoint_nodes.remove(breakpoint_node)

        # If no active node, push directly
        if not self._active_nodes:
            self._active_nodes.insert(0, logic_node)
            return

        # Otherwise, get current active node (top = index 0)
        active_node = self._active_nodes[0]
        placeholder = active_node._get_placeholder()
        active_node._replace(placeholder, logic_node)
        self._active_nodes.insert(0, logic_node)

    def _ln_exit(self, logic_node: LogicNode):
        if not self._active_nodes or self._active_nodes[0] is not logic_node:
            raise AssertionError("The LogicNode is not currently active.")
        self._active_nodes.pop(0)

    def shelve(self):
        shelved = {
            'active_groups': self._active_groups.copy(),
            'active_nodes': self._active_nodes.copy(),
            'breakpoint_nodes': self._breakpoint_nodes.copy(),
            'inspection_mode': self.inspection_mode,
            'vigilant_mode': self.vigilant_mode,
        }
        self._shelved_state.insert(0, shelved)

        # Reset to clean state
        self._active_groups = []
        self._active_nodes = []
        self._breakpoint_nodes = []

        return shelved

    def unshelve(self):
        if not self._shelved_state:
            raise RuntimeError("No shelved state to unshelve.")

        state = self._shelved_state.pop(0)

        self._active_groups = state['active_groups']
        self._active_nodes = state['active_nodes']
        self._breakpoint_nodes = state['breakpoint_nodes']
        self.inspection_mode = state['inspection_mode']
        self.vigilant_mode = state['vigilant_mode']

    def clear(self):
        self._cache.clear()
        self._active_groups.clear()
        self._active_nodes.clear()
        self._breakpoint_nodes.clear()

    @property
    def active_group(self) -> LogicGroup | None:
        return self._active_groups[0] if self._active_groups else None

    @property
    def active_node(self) -> LogicNode | None:
        return self._active_nodes[0] if self._active_nodes else None


LGM = LogicGroupManager()


class LogicGroup(object):
    def __init__(self, *, name: str = None, parent: LogicGroup = None, contexts: dict | None = None, **kwargs):
        self.name = f"{self.__class__.__name__}.{uuid.uuid4()}" if name is None else name

        if self in LGM:
            raise RuntimeError(f"LogicGroup {name} of type {self.__class__.__name__} already exists!")

        self.parent = parent
        self.Break = type(f"{self.__class__.__name__}Break", (BreakBlock,), {})
        self.contexts = {} if contexts is None else contexts

    def _break_inspection(self) -> None:
        # Case 1: No active node, breaks affect nothing
        if not LGM._active_nodes:
            return

        active_node = LGM._active_nodes[0]  # top of stack

        # Step 2.1: Locate placeholder and replace with breakpoint
        placeholder = active_node._get_placeholder()
        breakpoint_node = BreakpointNode()
        breakpoint_node.break_from = self
        active_node._replace(placeholder, breakpoint_node)

        # Step 2.2: Push to global breakpoint stack
        LGM._breakpoint_nodes.insert(0, breakpoint_node)

    def _break_active(self) -> None:
        if not LGM._active_groups:
            raise RuntimeError("No active LogicGroup to break from.")
        active_group = LGM._active_groups[0]
        if active_group is not self:
            raise IndexError('Not breaking from the top active LogicGroup.')
        raise self.Break()

    def _break_runtime(self) -> None:
        if not LGM._active_nodes:
            raise RuntimeError("No active node context to break from.")

        # Step 1: Validate that this group is in the active group stack
        if self not in LGM._active_groups:
            raise ValueError(f"Break scope {self} not in active LogicGroup stack.")

        # Step 2: Unwind the group stack from top until we hit `self`
        # We iterate from the top (end of list) downward
        for group in LGM._active_groups[:]:
            group._break_active()
            if group is self:
                break

    # === Python Interfaces ===

    def __repr__(self) -> str:
        return f'<{self.__class__.__name__}>({self.name!r})'

    def __enter__(self):
        LGM._lg_enter(self)
        return self

    def __exit__(self, exc_type: type | None, exc_value: BaseException | None, exc_traceback: Any) -> bool | None:
        LGM._lg_exit(self)

        if exc_type is None:
            return None

        # Suppress only the dynamically created Break exception for this group
        if issubclass(exc_type, self.Break):
            return True
        return False

    @classmethod
    def break_(cls, scope: LogicGroup = None) -> None:
        if scope is None:
            scope = LGM.active_group

        if scope is None:
            raise RuntimeError("No active LogicGroup to break from.")

        if LGM.inspection_mode:
            scope._break_inspection()
        else:
            scope._break_runtime()

    def break_active(self) -> None:
        self._break_active()

    def break_inspection(self) -> None:
        self._break_inspection()

    def break_runtime(self) -> None:
        self._break_runtime()


class LogicNode(LogicExpression):
    def __init__(self, *, expression: float | int | bool | Exception | Callable[[], Any], dtype: type = None, repr: str = None, uid: uuid.UUID = None):
        super().__init__(expression=expression, dtype=dtype, repr=repr, uid=uid)

        self.subordinates = []
        self.condition_to_parent = NO_CONDITION
        self.parent = None
        self.children = {}
        self.labels = [_.name for _ in LGM._active_groups]
        self.autogen = False

    def _infer_condition(self, child: LogicNode) -> NodeEdgeCondition:
        size = len(self.subordinates)

        # Case 1: No child node registered, the first child is always TRUE unless specified
        if size == 0:
            return TRUE_CONDITION

        last_node = self.subordinates[0]
        last_condition = last_node.condition_to_parent

        # Case 1: Only 1 child node registered, the second child is always opposite of the first
        if size == 1 and isinstance(last_condition, BinaryCondition):
            if last_condition is TRUE_CONDITION:
                return FALSE_CONDITION
            else:
                return TRUE_CONDITION

        # Case 2: Only 1 child node registered, and the first condition is not specified.
        if size == 1 and isinstance(last_condition, ConditionElse):
            # Case 2.1: If the child is auto generated, we can assume this child should be TRUE
            if last_node.autogen:
                return TRUE_CONDITION
            # Case 2.2: Otherwise, we cannot infer the condition

        if size == 1:
            raise EdgeValueError('Cannot auto-infer condition from single existing non-binary condition.')

        second_node = self.subordinates[1]
        second_condition = second_node.condition_to_parent
        # Case 3: check if any 2 node is from autogeneration
        # Returning the condition of the auto-generated node, so that it can be replaced later.
        if last_node.autogen:
            return last_condition
        elif second_node.autogen:
            return second_condition

        if size > 2:
            raise TooManyChildren(size)
        # Case 4: both conditions are non-binary, cannot infer
        raise EdgeValueError('Cannot auto-infer condition for non-binary existing condition.')

    def _get_placeholder(self) -> PlaceholderNode:
        if not self.subordinates:
            placeholder = PlaceholderNode(auto_connect=False)
            self._append(placeholder, TRUE_CONDITION)
            return placeholder

        # Case 2: Traverse the stack to find existing placeholder
        for node in self.subordinates:
            if isinstance(node, PlaceholderNode):
                return node

        # Case 3: No existing placeholder, create a new one with AUTO_CONDITION.
        placeholder = PlaceholderNode(auto_connect=False)
        # c_append will validate and infer the condition, and raise error as we intended.
        self._append(placeholder, AUTO_CONDITION)
        return placeholder

    def _append(self, child: LogicNode, condition: NodeEdgeCondition) -> None:
        if condition is None:
            raise ValueError("LogicNode must have an valid edge condition.")

        if condition is AUTO_CONDITION:
            condition = self._infer_condition(child)

        if condition in self.children:
            raise KeyError(f"Edge {condition} already registered.")

        self.children[condition] = child
        self.subordinates.insert(0, child)
        child.parent = self
        child.condition_to_parent = condition

    def _overwrite(self, new_node: LogicNode, condition: NodeEdgeCondition) -> None:
        if condition is None:
            raise ValueError("LogicNode must have an valid edge condition.")

        if condition is AUTO_CONDITION:
            condition = self._infer_condition(new_node)

        if condition not in self.children:
            raise KeyError(f"Edge {condition} not registered, cannot overwrite.")

        original_node = self.children[condition]
        self.children[condition] = new_node
        new_node.parent = self
        new_node.condition_to_parent = condition

        self.subordinates[self.subordinates.index(original_node)] = new_node
        original_node.parent = None
        original_node.condition_to_parent = NO_CONDITION

    def _replace(self, original_node: LogicNode, new_node: LogicNode) -> None:
        # The __eq__ of LogicExpression is overloaded, so we must check identity here.
        for node in LGM._active_nodes:
            if node is original_node:
                raise RuntimeError('Must not replace active node. Existing first required.')

        self.subordinates[self.subordinates.index(original_node)] = new_node

        self.children[original_node.condition_to_parent] = new_node
        new_node.parent = self
        new_node.condition_to_parent = original_node.condition_to_parent

        original_node.parent = None
        original_node.condition_to_parent = NO_CONDITION

    def _validate(self):
        if len(self.subordinates) != len(self.children):
            raise NodeValueError('Subordinate stack size does not match registered children.')

        for condition, child in self.children.items():
            if child.condition_to_parent is not condition:
                raise EdgeValueError('Child node condition does not match registered condition.')
            if node not in self.subordinates:
                raise ValueError(f"LogicNode {child} not found in stack")

    def _eval_recursively(self, path: list | None = None, default: Any = NO_DEFAULT) -> tuple[Any, list]:
        if path is None:
            path = [self]
        else:
            path.append(self)

        value = self._eval(False)
        if self.is_leaf:
            return value, path

        else_branch = None
        for child in self.subordinates:
            condition = child.condition_to_parent
            if condition is ELSE_CONDITION:
                else_branch = child
            elif condition is NO_CONDITION or value == condition.value:
                return child._eval_recursively(path, default)

        if else_branch is not None:
            return else_branch._eval_recursively(path, default)

        if default is NO_DEFAULT:
            raise ValueError(f"No matching condition found for value {value} at '{self.repr}'.")

        LOGGER.warning(f"No matching condition found for value {value} at '{self.repr}', using default {default}.")
        return default, path

    def _auto_fill(self) -> None:
        size = len(self.children)
        no_action = NoAction(auto_connect=False, autogen=True)

        # Case 1: No child node registered
        if size == 0:
            LOGGER.warning(f"{self} having no [True] branch. Check the <with> statement code block to see if this is intended.")
            self._append(no_action, NO_CONDITION)
            return

        node = self.subordinates[0]
        condition = node.condition_to_parent

        # Case 2: Single child node
        if size == 1:
            # Case 2.1: single child with no condition
            if condition is NO_CONDITION:
                return
            # Case 2.2: single child deliberately set to ELSE
            if condition is ELSE_CONDITION:
                LOGGER.warning(f"{self} having no [True] branch. Check the <with> statement code block to see if this is intended.")
                self._overwrite(no_action, TRUE_CONDITION)
                return
            # Case 2.4: single child with binary condition
            if isinstance(condition, BinaryCondition):
                self._append(no_action, ~condition)
                return

        # Case 3: Double Node
        second_node = self.subordinates[1]
        second_condition = second_node.condition_to_parent

        if size == 2:
            # Case 3.1: unconditioned branch not allowed with two branches
            if condition is NO_CONDITION or second_condition is NO_CONDITION:
                raise TooManyChildren('Cannot have unconditioned branch when there are two branches.')
            # Case 3.2: one branch is ELSE — valid
            if condition is ELSE_CONDITION or second_condition is ELSE_CONDITION:
                return
            # Case 3.3: both binary — valid
            if isinstance(condition, BinaryCondition) and isinstance(second_condition, BinaryCondition):
                return
            # Case 3.4: both non-binary — add protective else
            if not isinstance(condition, BinaryCondition) and not isinstance(second_condition, BinaryCondition):
                self._append(no_action, ELSE_CONDITION)
                return
            raise EdgeValueError(f'Conflicting conditions detected, {condition} and {second_condition}.')

        # Case 4: Multiple nodes (>2)
        else_detected = False
        for cond in self.children:
            if cond is NO_CONDITION:
                raise TooManyChildren('Cannot have unconditioned branch when there are multiple branches.')
            if cond is ELSE_CONDITION:
                else_detected = True
            if isinstance(cond, BinaryCondition):
                raise TooManyChildren('Cannot have binary branch when there are more than 2 branches.')
        if not else_detected:
            self._append(no_action, ELSE_CONDITION)

    def _consolidate_placeholder(self) -> int:
        placeholder_count = 0
        i = 0
        while i < len(self.subordinates):
            node = self.subordinates[i]
            if isinstance(node, PlaceholderNode):
                self._replace(node, NoAction(auto_connect=False, autogen=True))
                placeholder_count += 1
                # no increment: replacement maintains list length; continue checking same index
            else:
                i += 1
        return placeholder_count

    def _entry_check(self) -> bool:
        if LGM.inspection_mode:
            return True
        return bool(self._eval(False))

    def _on_enter(self) -> None:
        # Placeholders must be in reversed order, so that locating placeholder works correctly.
        self._append(PlaceholderNode(auto_connect=False), FALSE_CONDITION)
        self._append(PlaceholderNode(auto_connect=False), TRUE_CONDITION)
        LGM._ln_enter(self)

    def _on_exit(self) -> None:
        self._validate()
        self._auto_fill()
        self._consolidate_placeholder()
        LGM._ln_exit(self)

    # === Python Interfaces ===

    def __rshift__(self, other: LogicNode) -> LogicNode:
        self._append(other, AUTO_CONDITION)
        return other  # Allow chaining

    def __call__(self, default: Any = None) -> Any:
        if default is None:
            default = NoAction(auto_connect=False, autogen=True)
        inspection_mode = LGM.inspection_mode
        if inspection_mode:
            LOGGER.info('LGM inspection mode temporarily disabled to evaluate correctly.')
            LGM.inspection_mode = False
        try:
            path = []
            value, _ = self._eval_recursively(path, default)
            return value
        finally:
            LGM.inspection_mode = inspection_mode

    def __repr__(self) -> str:
        return f'<{self.__class__.__name__}>({self.repr!r})'

    def __hash__(self) -> int:
        return id(self)

    def append(self, child: LogicNode, condition: NodeEdgeCondition = AUTO_CONDITION) -> None:
        self._append(child, condition)

    def overwrite(self, new_node: LogicNode, condition: NodeEdgeCondition) -> None:
        self._overwrite(new_node, condition)

    def replace(self, original_node: LogicNode, new_node: LogicNode) -> None:
        self._replace(original_node, new_node)

    def eval_recursively(self, path: list | None = None, default: Any = NO_DEFAULT) -> tuple[Any, list]:
        return self._eval_recursively(path, default)

    def list_labels(self) -> dict[str, list[LogicNode]]:
        labels = {}

        def traverse(node: LogicNode):
            for group in node.labels:
                labels.setdefault(group, []).append(node)
            for child in node.children.values():
                traverse(child)

        traverse(self)
        return labels

    @property
    def leaves(self):
        if not self.subordinates:
            yield self
        else:
            for child in self.subordinates:
                yield from child.leaves

    @property
    def is_leaf(self) -> bool:
        return not self.children

    @property
    def child_stack(self):
        yield from self.subordinates

    @property
    def descendants(self):
        for child in self.subordinates:
            yield child
            yield from child.descendants
            frame = frame.prev


class BreakpointNode(LogicNode):
    def __init__(self, *, break_from: LogicGroup = None, expression: float | int | bool | Exception | Callable[[], Any] = None, dtype: type = None, repr: str = None, uid: uuid.UUID = None):
        super().__init__(
            expression=NoAction(auto_connect=False, autogen=True) if expression is None else expression,
            dtype=dtype,
            repr=repr,
            uid=uid
        )

        self.break_from = break_from
        self.autogen = True
        self.await_connection = False

    def _connect(self, child: LogicNode) -> None:
        if self.subordinates:
            raise TooManyChildren(f'{self.__class__.__name__} must not have more than one child node.')
        self._append(child, NO_CONDITION)
        self.await_connection = False

    def _on_enter(self) -> None:
        if self.subordinates:
            raise TooManyChildren(f'{self.__class__.__name__} must not have more than one child node.')
        self.await_connection = False
        try:
            LGM._breakpoint_nodes.remove(self)
        except NodeNotFountError as _:
            pass
        self._append(PlaceholderNode(auto_connect=False), NO_CONDITION)
        LGM._active_nodes.insert(0, self)

    def _on_exit(self) -> None:
        LGM._ln_exit(self)

    def _eval(self, enforce_dtype: bool) -> Any:
        if not self.subordinates:
            if LGM.vigilant_mode:
                raise NodeValueError(f'{self} not connected.')
            return self.expression

        linked_to = self.subordinates[0]
        return linked_to._eval(enforce_dtype)

    def _eval_recursively(self, path: list | None = None, default: Any = NO_DEFAULT) -> tuple[Any, list]:
        if path is None:
            path = []
        path.append(self)

        if not self.subordinates:
            if LGM.vigilant_mode:
                raise NodeValueError(f'{self} not connected.')
            return self.expression, path

        linked_to = self.subordinates[0]
        return linked_to._eval_recursively(path, default)

    def __repr__(self) -> str:
        if self.subordinates:
            return f'<{self.__class__.__name__} connected>(break_from={self.break_from})'
        elif self.await_connection:
            return f'<{self.__class__.__name__} active>(break_from={self.break_from})'
        elif self.parent:
            return f'<{self.__class__.__name__} idle>(break_from={self.break_from})'
        else:
            return f'<{self.__class__.__name__} dangling>(break_from={self.break_from})'

    @classmethod
    def break_(cls, break_from: LogicGroup, **kwargs):
        breakpoint_node = BreakpointNode(break_from=break_from, **kwargs)
        active_node = LGM.active_node
        placeholder = active_node._get_placeholder()
        active_node._replace(placeholder, breakpoint_node)
        return breakpoint_node

    def connect(self, child: LogicNode) -> None:
        self._connect(child)

    @property
    def linked_to(self) -> LogicNode | None:
        return self.subordinates[0] if self.subordinates else None


class ActionNode(LogicNode):
    def __init__(
            self,
            *,
            action: Callable[[], Any] | None = None,
            expression: Any = None,
            dtype: type = None,
            repr: str | None = None,
            uid: uuid.UUID = None,
            auto_connect: bool = True,
            **kwargs
    ):
        # Do not capture logic group labels — action nodes are leaves
        super().__init__(expression=expression, dtype=dtype, repr=repr, uid=uid)

        self.action = action

        if auto_connect:
            self._auto_connect()

    def _auto_connect(self) -> None:
        if not LGM._active_nodes:
            if LGM.vigilant_mode:
                raise NodeValueError(f'Cannot set ActionNode {self} as root node.')
            return

        active_node = LGM._active_nodes[0]  # top of stack
        placeholder = active_node._get_placeholder()
        active_node._replace(placeholder, self)

    def _post_eval(self) -> None:
        if self.action is not None:
            self.action()

    def _append(self, child: LogicNode, condition: NodeEdgeCondition) -> None:
        raise TooManyChildren('Action node must not have any child node.')

    def _on_enter(self) -> None:
        raise NodeContextError('ActionNode does not support context management with <with> statement.')

    def _on_exit(self) -> None:
        pass

    def _eval_recursively(self, path: list | None = None, default: Any = NO_DEFAULT) -> tuple[Any, list]:
        if path is None:
            path = []
        path.append(self)

        value = self._eval(False)
        self._post_eval()

        if not self.is_leaf:
            raise TooManyChildren('Action node must not have any child node.')

        return value, path


class PlaceholderNode(ActionNode):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)

        self.autogen = True
        self.action = NoAction(auto_connect=False, autogen=True)

    def _eval(self, enforce_dtype: bool) -> Any:
        if LGM.vigilant_mode:
            return self
        return self.action


class NoAction(ActionNode):
    def __init__(self, sig: int = 0, repr='NoAction', autogen: bool = False, **kwargs):
        super().__init__(repr=repr, **kwargs)
        self.sig = sig
        self.autogen = autogen

    def _eval(self, enforce_dtype: bool) -> Any:
        return self

    def __repr__(self) -> str:
        return f'<{self.__class__.__name__}>(sig={self.sig})'

    def __int__(self):
        return self.sig


class LongAction(ActionNode):
    def __init__(self, *, sig: int = 1, repr='LongAction', **kwargs):
        super().__init__(repr=repr, **kwargs)
        self.sig = sig

    def _eval(self, enforce_dtype: bool) -> Any:
        return self

    def __repr__(self) -> str:
        return f'<{self.__class__.__name__}>(sig={self.sig})'

    def __int__(self):
        return self.sig


class ShortAction(ActionNode):
    def __init__(self, *, sig: int = -1, repr='ShortAction', **kwargs):
        super().__init__(repr=repr, **kwargs)
        self.sig = sig

    def _eval(self, enforce_dtype: bool) -> Any:
        return self

    def __repr__(self) -> str:
        return f'<{self.__class__.__name__}>(sig={self.sig})'

    def __int__(self):
        return self.sig


class CancelAction(ActionNode):
    def __init__(self, sig: int = 0, repr='CancelAction', **kwargs):
        super().__init__(repr=repr, **kwargs)
        self.sig = sig

    def _eval(self, enforce_dtype: bool) -> Any:
        return self

    def __repr__(self) -> str:
        return f'<{self.__class__.__name__}>(sig={self.sig})'

    def __int__(self):
        return self.sig


class ClearAction(ActionNode):
    def __init__(self, sig: int = 0, repr='ClearAction', **kwargs):
        super().__init__(repr=repr, **kwargs)
        self.sig = sig

    def _eval(self, enforce_dtype: bool) -> Any:
        return self

    def __repr__(self) -> str:
        return f'<{self.__class__.__name__}>(sig={self.sig})'

    def __int__(self):
        return self.sig
