from __future__ import annotations

import inspect
import json
import operator
import sys
import traceback
from collections.abc import Callable, Iterable
from typing import Any, Self, final

from . import LOGGER
from .exc import TooFewChildren, TooManyChildren, EdgeValueError, NodeValueError, NodeNotFountError

LOGGER = LOGGER.getChild('abc')

__all__ = ['LGM', 'LogicGroup', 'SkipContextsBlock', 'LogicExpression', 'ExpressionCollection', 'LogicNode', 'ActionNode', 'ELSE_CONDITION']


class Singleton(type):
    _instances = {}

    def __call__(cls, *args, **kwargs):
        if cls not in cls._instances:
            cls._instances[cls] = super(Singleton, cls).__call__(*args, **kwargs)
        return cls._instances[cls]


class ConditionElse(object):
    """Represents an else condition in decision trees."""

    def __str__(self):
        return ""


ELSE_CONDITION = NO_CONDITION = ConditionElse()


class LogicGroupManager(metaclass=Singleton):
    """
    A singleton class to manage caching and reuse of LogicGroup instances.
    Keeps track of active LogicGroup instances using a cursor.
    """

    def __init__(self):
        # Dictionary to store cached LogicGroup instances
        self._cache = {}

        # Cursor to track the currently active LogicGroups
        self._active_groups: list[LogicGroup] = []
        self._active_nodes: list[LogicNode] = []
        self._breakpoint_nodes: list[ActionNode] = []  # action nodes, usually NoAction() nodes, marked as an early-exit (breakpoint) of a logic group
        self._pending_connection_nodes: list[ActionNode] = []  # for those breakpoint-nodes, they will be activated when the corresponding logic group is finalized.
        self._shelved_state = []  # shelve state to support temporally initialize a separate node-graph
        self.inspection_mode = False
        self.vigilant_mode = False

    def __call__(self, name: str, cls: type[LogicGroup], **kwargs) -> LogicGroup:
        """
        Retrieve a cached LogicGroup instance or create a new one if not cached.

        :param name: The name of the LogicGroup.
        :param cls: The class of the LogicGroup to create if not cached.
        :param kwargs: Additional arguments for LogicGroup initialization.
        :return: A LogicGroup instance.
        """
        if name in self._cache:
            return self._cache[name]

        # Create a new instance and add it to the cache
        logic_group = cls(name=name, **kwargs)
        self._cache[name] = logic_group
        return logic_group

    def __contains__(self, name: str) -> bool:
        return name in self._cache

    def __getitem__(self, name: str) -> LogicGroup:
        return self._cache[name]

    def __setitem__(self, name: str, value: LogicGroup):
        self._cache[name] = value

    def enter_logic_group(self, logic_group: LogicGroup):
        """
        Append a LogicGroup to the active list when it enters.

        :param logic_group: The LogicGroup entering the context.
        """
        self._active_groups.append(logic_group)

    def exit_logic_group(self, logic_group: LogicGroup):
        """
        Handle the exit of a LogicGroup and ensure subsequent groups also exit.

        :param logic_group: The LogicGroup exiting the context.
        """
        if not self._active_groups or self._active_groups[-1] is not logic_group:
            raise ValueError("The LogicGroup is not currently active.")

        self._active_groups.pop(-1)

        for node in self._breakpoint_nodes:
            if getattr(node, 'break_from') is logic_group:
                self._pending_connection_nodes.append(node)

    def enter_expression(self, node: LogicNode):
        if isinstance(self, ActionNode):
            LOGGER.error('Enter the with code block of an ActionNode rejected. Check is this intentional?')

        if self._pending_connection_nodes:
            from .node import NoAction

            for _exit_node in self._pending_connection_nodes:
                if isinstance(_exit_node, NoAction):
                    if (parent := _exit_node.parent) is None:
                        raise NodeNotFountError('ActionNode must have a parent node!')
                    parent.replace(original_node=_exit_node, new_node=node)
                else:
                    _exit_node.edges.append(NO_CONDITION)
                    _exit_node.nodes[NO_CONDITION] = node

            self._pending_connection_nodes.clear()

        if (active_node := self.active_expression) is not None:
            active_node: LogicNode = active_node
            active_node.subordinates.append(node)

        self._active_nodes.append(node)

    def exit_expression(self, node: LogicNode):
        if not self._active_nodes or self._active_nodes[-1] is not node:
            raise ValueError(f"The {node} is not currently active.")

        self._active_nodes.pop(-1)

    def shelve(self):
        shelved_state = dict(
            active_nodes=self._active_nodes.copy(),
            breakpoint_nodes=self._breakpoint_nodes.copy(),
            pending_connection_nodes=self._pending_connection_nodes.copy()
        )

        self._active_nodes.clear()
        self._breakpoint_nodes.clear()
        self._pending_connection_nodes.clear()

        self._shelved_state.append(shelved_state)
        return shelved_state

    def unshelve(self, reset_active: bool = True, reset_breakpoints: bool = True, reset_pending: bool = True):
        shelved_state = self._shelved_state.pop(-1)

        if reset_active:
            self._active_nodes.clear()

        if reset_breakpoints:
            self._breakpoint_nodes.clear()

        if reset_pending:
            self._pending_connection_nodes.clear()

        self._active_nodes[:0] = shelved_state['active_nodes']
        self._breakpoint_nodes[:0] = shelved_state['breakpoint_nodes']
        self._pending_connection_nodes[:0] = shelved_state['pending_connection_nodes']

        return shelved_state

    def clear(self):
        """
        Clear the cache of LogicGroup instances and reset active groups.
        """
        self._cache.clear()
        self._active_groups.clear()
        self._active_nodes.clear()

    @property
    def active_logic_group(self) -> LogicGroup | None:
        if self._active_groups:
            return self._active_groups[-1]

        return None

    @property
    def active_expression(self) -> LogicNode | None:
        if self._active_nodes:
            return self._active_nodes[-1]

        return None


LGM = LogicGroupManager()


class LogicGroupMeta(type):
    """
    A metaclass for LogicGroup that manages caching of instances.
    """
    _registry_ = {}

    def __new__(cls, name, bases, dct):
        new_class = super().__new__(cls, name, bases, dct)
        cls._registry_[name] = new_class
        return new_class

    def __call__(cls, name, *args, **kwargs):
        if name is None:
            raise ValueError("LogicGroup instances must have a 'name'.")

        # Check the cache for an existing instance
        if name in LGM:
            return LGM[name]

        # Create a new instance and cache it
        instance = super().__call__(name=name, *args, **kwargs)
        LGM[name] = instance
        return instance

    @property
    def registry(self):
        return self._registry_


class LogicGroup(object, metaclass=LogicGroupMeta):
    """
    A minimal context manager to save/restore state from the `.contexts` dict.

    A logic group maintains no status itself; the status should be restored
    from the outer `.contexts` dict.
    """

    def __init__(self, name: str, parent: Self = None, contexts: dict[str, Any] = None):
        self.name = name
        self.parent = parent
        self.Break = type(f"{self.__class__.__name__}Break", (Exception,), {})  # Assign Break at instance level

        # a root logic group
        if parent is None:
            info_dict = {}
            if contexts is None:
                contexts = {}
        # try to recover from parent
        else:
            info_dict = parent._sub_logics.setdefault(name, {})
            logic_type = self.__class__.__name__
            assert info_dict.setdefault('logic_type', logic_type) == logic_type, f"Logic {info_dict['logic_type']} already registered in {parent.name}!"
            contexts = info_dict.setdefault('contexts', {} if contexts is None else contexts)

        self.contexts: dict[str, Any] = contexts
        self._sub_logics = info_dict.setdefault('sub_logics', {})

    def __repr__(self):
        return f'<{self.__class__.__name__}>({self.name!r})'

    def __enter__(self) -> Self:
        LGM.enter_logic_group(self)
        return self

    def __exit__(self, exc_type, exc_value, exc_traceback):
        LGM.exit_logic_group(self)

        if exc_type is None:
            return

        if exc_type is self.Break:
            return True

        # Explicitly re-raise other exceptions
        return False

    def break_(self, scope: LogicGroup = None):
        if scope is None:
            scope = self

        # will not break from scope in inspection mode
        if LGM.inspection_mode:
            active_node = LGM.active_expression

            if active_node is not None:
                active_node: LogicNode
                if not active_node.nodes:
                    if LGM.vigilant_mode:
                        raise TooFewChildren()
                    else:
                        LOGGER.warning('Must have at least one action node before breaking from logic group. A NoAction node will be automatically assigned.')
                        from .node import NoAction
                        NoAction()

                last_node = active_node.last_leaf
                assert isinstance(last_node, ActionNode), NodeValueError('An ActionNode is required before breaking a LogicGroup.')
                last_node.break_from = scope
                LGM._breakpoint_nodes.append(last_node)
            return

        raise scope.Break()

    @property
    def sub_logics(self) -> dict[str, Self]:
        sub_logic_instances = {}
        for logic_name, info in self._sub_logics.items():
            logic_type = info["logic_type"]

            # Dynamically retrieve the class using meta registry
            logic_class = self.__class__.registry.get(logic_type)

            if logic_class is None:
                raise ValueError(f"Class {logic_type} not found in registry.")

            # Get the __init__ method's signature
            init_signature = inspect.signature(logic_class.__init__)
            init_params = init_signature.parameters

            # Prepare arguments for the sub-logic initialization
            init_args = {}
            for param_name, param in init_params.items():
                if param_name == "self":
                    continue  # Skip 'self'

                if param_name in info:
                    init_args[param_name] = info[param_name]
                elif param_name == "name":
                    init_args["name"] = logic_name
                elif param_name == "parent":
                    init_args["parent"] = self
                elif param_name == "contexts":
                    LOGGER.warning(f"Contexts dict not found for {logic_name}!")
                    init_args["contexts"] = {}
                elif param.default == inspect.Parameter.empty:
                    # Missing a required argument that cannot be inferred
                    raise TypeError(f"Missing required argument '{param_name}' for {logic_type}.")

            # Instantiate the sub-logic
            sub_logic_instance = logic_class(**init_args)
            sub_logic_instances[logic_name] = sub_logic_instance

        return sub_logic_instances


class SkipContextsBlock(object):
    class _Skip(Exception):
        pass

    def _entry_check(self) -> Any:
        """
        A True value indicating NOT skip.
        a False value indicating skip the code block.
        """
        pass

    @final
    def __enter__(self):
        if self._entry_check():  # Check if the expression evaluates to True
            self._on_enter()
            return self

        self._original_trace = self.get_trace()
        frame = inspect.currentframe().f_back
        sys.settrace(self.empty_trace)
        frame.f_trace = self.err_trace

    @final
    def __exit__(self, exc_type, exc_value, exc_traceback):
        if exc_type is None:
            self._on_exit()
            return

        if issubclass(exc_type, self._Skip):
            if hasattr(self, '_original_trace'):
                sys.settrace(self._original_trace)  # Restore the original trace
            else:
                raise Exception('original_trace not found! Debugger broken! This should never happened.')
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
        """
        Safely retrieve the current trace function, prioritizing the PyDev debugger's trace function.
        """
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

    @classmethod
    def empty_trace(cls, *args, **kwargs) -> None:
        pass

    @classmethod
    def err_trace(cls, frame, event, arg):
        raise cls._Skip("Expression evaluated to be False, cannot enter the block.")


class LogicExpression(SkipContextsBlock):
    """
    Represents a logical or mathematical expression that supports deferred evaluation.
    """

    def __init__(
            self,
            expression: float | int | bool | Exception | Callable[[], Any],
            dtype: type = None,
            repr: str = None,
    ):
        """
        Initialize the LogicExpression.

        Args:
            expression (Union[Any, Callable[[], Any]]): A callable or static value.
            dtype (type, optional): The expected type of the evaluated value (float, int, or bool).
            repr (str, optional): A string representation of the expression.
        """
        self.expression = expression
        self.dtype = dtype
        self.repr = repr if repr is not None else str(expression)

        super().__init__()

    def _entry_check(self) -> Any:
        return self.eval()

    def eval(self, enforce_dtype: bool = False) -> Any:
        """Evaluate the expression."""
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

    # Logical operators
    @classmethod
    def cast(cls, value: int | float | bool | Exception | Self, dtype: type = None) -> Self:
        """
        Convert a static value, callable, or error into a LogicExpression.

        Args:
            value (Union[int, float, bool, LogicExpression, Callable, Exception]):
                The value to convert. Can be:
                - A static value (int, float, or bool).
                - A callable returning a value.
                - A pre-existing LogicExpression.
                - An Exception to raise during evaluation.
            dtype (type, optional): The expected type of the resulting LogicExpression.
                If None, it will be inferred from the value.

        Returns:
            LogicExpression: The resulting LogicExpression.

        Raises:
            TypeError: If the value type is unsupported or dtype is incompatible.
        """
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

    def __eq__(self, other: int | float | bool | str | Self) -> Self:
        if isinstance(other, LogicExpression):
            other_value = other.eval()
        else:
            other_value = other

        return LogicExpression(
            expression=lambda: self.eval() == other_value,
            dtype=bool,
            repr=f"({self.repr} == {repr(other_value)})"
        )

    def __or__(self, other: Self | bool) -> Self:
        other_expr = self.cast(value=other, dtype=bool)
        new_expr = LogicExpression(
            expression=lambda: self.eval() or other_expr.eval(),
            dtype=bool,
            repr=f"({self.repr} or {other_expr.repr})"
        )
        return new_expr

    # Math operators
    @classmethod
    def _math_op(cls, self: Self, other: int | float | Self, op: Callable, operator_str: str, dtype: type = None) -> Self:
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
        return self._math_op(self=self, other=other, op=operator.add, operator_str="+")

    def __sub__(self, other: int | float | bool | Self) -> Self:
        return self._math_op(self=self, other=other, op=operator.sub, operator_str="-")

    def __mul__(self, other: int | float | bool | Self) -> Self:
        return self._math_op(self=self, other=other, op=operator.mul, operator_str="*")

    def __truediv__(self, other: int | float | bool | Self) -> Self:
        return self._math_op(self=self, other=other, op=operator.truediv, operator_str="/")

    def __floordiv__(self, other: int | float | bool | Self) -> Self:
        return self._math_op(self=self, other=other, op=operator.floordiv, operator_str="//")

    def __pow__(self, other: int | float | bool | Self) -> Self:
        return self._math_op(self=self, other=other, op=operator.pow, operator_str="**")

    # Comparison operators, note that __eq__, __ne__ is special and should not implement as math operator
    def __lt__(self, other: int | float | bool | Self) -> Self:
        return self._math_op(self=self, other=other, op=operator.lt, operator_str="<", dtype=bool)

    def __le__(self, other: int | float | bool | Self) -> Self:
        return self._math_op(self=self, other=other, op=operator.le, operator_str="<=", dtype=bool)

    def __gt__(self, other: int | float | bool | Self) -> Self:
        return self._math_op(self=self, other=other, op=operator.gt, operator_str=">", dtype=bool)

    def __ge__(self, other: int | float | bool | Self) -> Self:
        return self._math_op(self=self, other=other, op=operator.ge, operator_str=">=", dtype=bool)

    def __repr__(self) -> str:
        return f"LogicExpression(dtype={'Any' if self.dtype is None else self.dtype.__name__}, repr={self.repr})"


class ExpressionCollection(LogicGroup):
    def __init__(self, data: Any, name: str, **kwargs):
        if 'logic_group' not in kwargs:
            logic_group = kwargs.get("logic_group")
        else:
            logic_group = LGM.active_logic_group

        super().__init__(
            name=name if name is not None else f'{logic_group.name}.{self.__class__.__name__}',
            parent=logic_group
        )

        self.data = self.contexts.setdefault('data', data)


class LogicNode(LogicExpression):
    def __init__(
            self,
            expression: float | int | bool | Exception | Callable[[], Any],
            dtype: type = None,
            repr: str = None,
    ):
        """
        Initialize the LogicExpression.

        Args:
            expression (Union[Any, Callable[[], Any]]): A callable or static value.
            dtype (type, optional): The expected type of the evaluated value (float, int, or bool).
            repr (str, optional): A string representation of the expression.
        """
        super().__init__(expression=expression, dtype=dtype, repr=repr)

        self.labels = [_.name for _ in LGM._active_groups]
        self.nodes: dict[Any, LogicNode] = {}  # Dict[condition, LogicExpression]
        self.parent: LogicNode | None = None
        self.edges = []  # list of condition
        self.subordinates = []  # all the subordinate nodes initialized inside this node with statement

    def _entry_check(self) -> Any:
        """
        If `LGM.inspection_mode` is active, always returns `True`.
        Which guarantees the entrance the with code block

        Returns:
            Any: Evaluation result.
        """
        if LGM.inspection_mode:
            return True
        return self.eval()

    def __rshift__(self, expression: Self):
        """Overloads >> operator for adding child nodes."""
        self.append(expression)
        return expression  # Allow chaining

    def __call__(self, default=None) -> Any:
        """
        Recursively evaluates the decision tree starting from this node.

        Keyword Args:
            default (Any, optional): Fallback value if no matching condition is found.

        Returns:
            final_value (Any): The evaluated result of the tree.

        Raises:
            ValueError: If no matching condition is found and no default value is provided.
        """

        if default is None:
            from .node import NoAction
            default = NoAction(auto_connect=False)

        if _ins_mode := LGM.inspection_mode:
            LOGGER.info('LGM inspection mode temporally disabled to evaluate correctly.')
            LGM.inspection_mode = False

        _, path = self.eval_recursively(default=default)
        LGM.inspection_mode = _ins_mode
        if not path:
            raise TooFewChildren()

        leaf = path[-1]
        return leaf.eval()

    def __repr__(self):
        return f'<{self.__class__.__name__}>({self.repr!r})'

    def _on_enter(self):
        active_node: LogicNode = LGM.active_expression

        if active_node is None:
            return LGM.enter_expression(node=self)

        match active_node.subordinates:
            case []:
                active_node.append(expression=self, edge_condition=True)

            case [*_, last_node] if not last_node.nodes:
                raise TooFewChildren()

            case [*_, last_node] if len(last_node.nodes) == 1:
                edge_condition = last_node.last_edge
                if not isinstance(edge_condition, bool):
                    raise EdgeValueError(f'{last_node} Edge condition must be a Boolean!')
                last_node.append(expression=self, edge_condition=not edge_condition)

            case [*_, last_node] if len(last_node.nodes) == 2:
                from .node import NoAction
                edge_condition, child = last_node.last_edge, last_node.last_node
                if not isinstance(child, NoAction):
                    raise NodeValueError(f'{last_node} second child node must be a NoAction node!')
                last_node.pop(-1)
                last_node.append(expression=self, edge_condition=edge_condition)

            case [*_, last_node] if len(last_node.nodes) > 2:
                raise TooManyChildren()

        if isinstance(self, ActionNode):
            pass
        else:
            LGM.enter_expression(node=self)

    def _on_exit(self):
        self.fill_binary_branch(node=self)
        LGM.exit_expression(node=self)

    @classmethod
    def fill_binary_branch(cls, node: LogicNode, with_action: ActionNode = None):
        """
        Ensures the decision tree node has both True and False branches.

        Args:
            node (LogicNode): The node to check.
            with_action (ActionNode, optional): A default action node to add if missing.
        """
        if with_action is None:
            from .node import NoAction
            with_action = NoAction(auto_connect=False)

        if isinstance(node, ActionNode):
            return

        match len(node.nodes):
            case 0:
                LOGGER.warning(f"It is rear that {node} having no True branch. Check the <with> statement code block to see if this is intended.")
                node.append(expression=with_action, edge_condition=False)
            case 1:
                edge_condition = node.last_edge
                if not isinstance(edge_condition, bool):
                    raise EdgeValueError(f'{node} Edge condition must be a Boolean!')
                node.append(expression=with_action, edge_condition=not edge_condition)
            case _:
                raise TooManyChildren()

    @classmethod
    def traverse(cls, node: Self, G=None, node_map: dict[int, Self] = None, parent: Self = None, edge_condition: Any = None):
        """
        Recursively traverses the decision tree, adding nodes and edges to the graph.

        Args:
            node (LogicNode): The current node being traversed.
            G (networkx.DiGraph, optional): The graph being constructed. Defaults to a new graph.
            node_map (dict, optional): A dictionary mapping node IDs to LogicNode instances.
            parent (LogicNode, optional): The parent node of the current node.
            edge_condition (Any, optional): The condition from parent to this node.
        """
        import networkx as nx

        if G is None:
            G = nx.DiGraph()
        if node_map is None:
            node_map = {}

        node_id = id(node)
        # if node_id in node_map:
        #     return  # Avoid duplicate traversal

        node_map[node_id] = node
        G.add_node(node_id, description=node.repr)

        if parent is not None:
            edge_label = str(edge_condition)  # Use the edge condition from the parent's children list
            G.add_edge(id(parent), node_id, label=edge_label)

        for edge_condition, child in node.nodes.items():
            cls.traverse(node=child, G=G, node_map=node_map, parent=node, edge_condition=edge_condition)

        return G, node_map

    def append(self, expression: LogicNode, edge_condition: Any = None):
        """
        Adds a child node to the current node.

        Args:
            expression (LogicNode): The child node.
            edge_condition (Any, optional): The condition for branching.

        Raises:
            ValueError: If no edge condition is provided.
        """
        if edge_condition is None:
            edge_condition = NO_CONDITION

        if edge_condition is None:
            raise ValueError("Child LogicExpression must have an edge condition.")

        if edge_condition in self.nodes:
            raise ValueError(f"Edge {edge_condition} already exists.")

        self.edges.append(edge_condition)
        self.nodes[edge_condition] = expression
        expression.parent = self

    def pop(self, index: int = -1) -> tuple[Any, LogicNode]:
        edge = self.edges.pop(index)
        node = self.nodes.pop(edge)
        return edge, node

    def replace(self, original_node: LogicNode, new_node: LogicNode):
        for condition, node in self.nodes.items():
            if node is original_node:
                break
        else:
            raise NodeNotFountError()

        self.nodes[condition] = new_node

    def eval_recursively(self, **kwargs):
        """
        Recursively evaluates the decision tree starting from this node.

        Keyword Args:
            path (list, optional): Tracks the decision path during evaluation. Defaults to a new list.
            default (Any, optional): Fallback value if no matching condition is found.

        Returns:
            tuple: (final_value, decision_path)
                - final_value (Any): The evaluated result of the tree.
                - decision_path (list): The sequence of nodes traversed during evaluation.

        Raises:
            ValueError: If no matching condition is found and no default value is provided.
        """
        if 'path' in kwargs:
            path = kwargs['path']
        else:
            path = [self]

        value = self.eval()

        if not self.nodes:
            return value, path

        for condition, child in self.nodes.items():
            if condition == value or condition is NO_CONDITION:
                return child.eval_recursively(path=path)

        if 'default' in kwargs:
            default = kwargs['default']
            LOGGER.info(f"No matching condition found for value {value} at '{self.repr}', using default {default}.")
            return default, path

        raise ValueError(f"No matching condition found for value {value} at '{self.repr}'.")

    def list_labels(self) -> dict[str, list[LogicNode]]:
        """
        Lists all logic groups in the tree and returns a dictionary mapping group names to nodes.
        """
        labels = {}

        def traverse(node):
            for group in node.labels:
                if group not in labels:
                    labels[group] = []
                labels[group].append(node)
            for _, child in node.nodes.items():
                traverse(child)

        traverse(self)
        return labels

    def select_node(self, label: str) -> LogicNode | None:
        """
        Selects the root node of a logic group and validates that the group is chained.
        """
        labels = self.list_labels()
        if label not in labels:
            return None

        nodes = labels[label]
        root = None

        for node in nodes:
            if not any(node in child_nodes for _, child_nodes in labels.items() if _ != label):
                if root is not None:
                    raise ValueError(f"Logic group '{label}' has multiple roots.")
                root = node

        return root

    def to_html(self, with_group=True, dry_run=True, filename="decision_graph.html", **kwargs):
        """
        Visualizes the decision tree using PyVis.
        If dry_run=True, shows structure without highlighting active path.
        If dry_run=False, evaluates the tree and highlights the decision path.
        If with_group=True, uses grouped logic view.
        """
        from pyvis.network import Network

        G, node_map = self.traverse(self)
        # Highlight path if not in dry run
        activated_path = []
        if not dry_run:
            try:
                _, path = self.eval_recursively()
                activated_path = [id(node) for node in path]
            except Exception:
                activated_path.clear()
                dry_run = True
                LOGGER.error(f"Failed to evaluate decision tree.\n{traceback.format_exc()}")

        # Visualization using PyVis
        net = Network(
            height=kwargs.get('height', "750px"),
            width=kwargs.get('width', "100%"),
            directed=True,
            notebook=False,
            neighborhood_highlight=True
        )
        default_color = kwargs.get('default_color', "lightblue")
        highlight_color = kwargs.get('highlight_color', "lightgreen")
        activated_color = kwargs.get('selected_color', "lightyellow")
        dimmed_color = kwargs.get('dimmed_color', "#e0e0e0")
        logic_shape = kwargs.get('logic_shape', "box")
        action_shape = kwargs.get('action_shape', "ellipse")

        original_colors = {}

        # Add nodes with group information
        for node_id, node in node_map.items():
            label = node.repr
            title = f"Node: {node.repr}"

            # Track the original color for each node
            node_color = activated_color if node_id in activated_path else default_color
            original_colors[node_id] = node_color

            if with_group:
                net.add_node(node_id, label=label, title=title, color=node_color, shape=action_shape if isinstance(node, ActionNode) else logic_shape, groups=node.labels)
            else:
                net.add_node(node_id, label=label, title=title, color=node_color, shape=action_shape if isinstance(node, ActionNode) else logic_shape)

        # Add edges
        for source, target, data in G.edges(data=True):
            edge_label = data.get("label", "")
            edge_color = "black" if dry_run else ("green" if source in activated_path and target in activated_path else "black")
            net.add_edge(source, target, label=edge_label, title=edge_label, color=edge_color, arrows="to")

        # Configure layout and options
        options = {
            "layout": {
                "hierarchical": {
                    "enabled": True,
                    "direction": "UD",  # UD = Up-Down (root at top, leaves at bottom)
                    "sortMethod": "directed",
                    "nodeSpacing": 150,
                    "levelSeparation": 200
                }
            },
            "physics": {
                "hierarchicalRepulsion": {
                    "centralGravity": 0.0,
                    "springLength": 200,
                    "springConstant": 0.01,
                    "nodeDistance": 200,
                    "damping": 0.09
                },
                "minVelocity": 0.75,
                "solver": "hierarchicalRepulsion"
            },
            "nodes": {
                "shape": "box",
                "shapeProperties": {"borderRadius": 10},
                "font": {"size": 14}
            },
            "edges": {
                "color": "black",
                "smooth": True
            }
        }

        net.set_options(json.dumps(options))

        # Generate the base HTML
        html = net.generate_html()

        # Inject custom controls and JavaScript
        buttons_html = """
        <div style="position: absolute; top: 10px; left: 10px; z-index: 1000; 
                    background: rgba(255, 255, 255, 0.9); padding: 12px; 
                    border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.3);
                    font-family: Arial, sans-serif;">

            <h4 style="margin: 0 0 10px; font-size: 16px; text-align: center; color: #333;">
                Decision Tree Controls
            </h4>

            <button onclick="resetColors()" class="control-btn">Reset</button>
        """

        if with_group:
            groups = {group for node in node_map.values() for group in node.labels}
            for group in sorted(groups):
                buttons_html += f'<button onclick="highlightGroup(\'{group}\')" class="control-btn">{group}</button>'
        buttons_html += "</div>"

        js_code = f"""
        <script>
        function resetColors() {{
            // Reset all nodes to their original color and opacity
            nodes.forEach(function(node) {{
                nodes.update([{{ 
                    id: node.id,
                    color: originalColors[node.id],  // Reset to original color
                    opacity: 1
                }}]);
            }});

            // Reset all edges to default color and opacity
            edges.forEach(function(edge) {{
                edges.update([{{ 
                    id: edge.id,
                    color: "black",
                    opacity: 1
                }}]);
            }});
        }}

        function highlightGroup(group) {{
            // Dim all nodes and edges first
            nodes.update([...nodes.getIds().map(id => ({{
                id: id,
                color: "{dimmed_color}",
                opacity: 0.3
            }}))]);

            edges.update([...edges.getIds().map(id => ({{
                id: id,
                color: "gray",
                opacity: 0.2
            }}))]);

            // Highlight nodes in the selected group
            const groupNodes = nodes.get({{
                filter: node => node.groups.includes(group)
            }});

            nodes.update([...groupNodes.map(node => ({{
                id: node.id,
                color: "{highlight_color}",
                opacity: 1
            }}))]);

            // Highlight connected edges
            const connectedEdges = edges.get({{
                filter: edge => 
                    groupNodes.some(n => n.id === edge.from) ||
                    groupNodes.some(n => n.id === edge.to)
            }});

            edges.update([...connectedEdges.map(edge => ({{
                id: edge.id,
                color: "black",
                opacity: 1
            }}))]);
        }}

        // Store the original node colors for reset functionality
        const originalColors = {json.dumps(original_colors)};
        </script>
        """

        # Inject better styles for buttons
        css_styles = """
        <style>
            .control-btn {
                background-color: #007BFF;
                color: white;
                border: none;
                padding: 8px 14px;
                margin: 5px;
                font-size: 14px;
                border-radius: 5px;
                cursor: pointer;
                transition: background 0.3s ease;
            }

            .control-btn:hover {
                background-color: #0056b3;
            }

            .control-btn:active {
                background-color: #003f7f;
            }
        </style>
        """

        # Insert custom elements into the HTML
        html = html.replace("</head>", f"{css_styles}</head>")
        html = html.replace("</body>", f"{buttons_html}{js_code}</body>")

        # Save the modified HTML
        with open(filename, "w") as f:
            f.write(html)

        LOGGER.info(f"Decision tree saved to {filename}")

    @property
    def children(self) -> Iterable[tuple[Any, LogicNode]]:
        """Returns an iterable of (edge, node) pairs."""
        return iter(self.nodes.items())

    @property
    def leaves(self) -> Iterable[LogicNode]:
        """Recursively finds and returns all leaf nodes (nodes without children)."""
        if not self.nodes:  # If no children, this node is a leaf
            yield self
        else:
            for _, child in self.nodes.items():  # Recursively get leaves from children
                yield from child.leaves

    @property
    def last_edge(self) -> Any:
        return self.edges[-1]

    @property
    def last_node(self) -> LogicNode:
        return self.nodes[self.last_edge]

    @property
    def last_leaf(self) -> LogicNode:
        if not self.nodes:
            return self
        return self.last_node.last_leaf

    @property
    def last_leaf_expression(self) -> LogicNode:
        last_leaf = self.last_leaf
        if isinstance(last_leaf, ActionNode):
            return last_leaf.parent
        return last_leaf


class ActionNode(LogicNode):
    def __init__(
            self,
            action: Callable[[], Any] | None = None,
            repr: str = None,
            auto_connect: bool = True
    ):
        """
        Initialize the LogicExpression.

        Args:
            action (Union[Any, Callable[[], Any]]): The action to execute.
            repr (str, optional): A string representation of the expression.
            auto_connect: auto-connect to the current active decision graph.
        """
        super().__init__(expression=True, repr=repr)
        self.action = action

        if auto_connect:
            super()._on_enter()

    def _on_enter(self):
        LOGGER.warning(f'{self.__class__.__name__} should not use with claude')

    def _on_exit(self):
        pass

    def _post_eval(self):
        """
        override this method to perform clean up functions.
        """

        if self.action is not None:
            self.action()

    def eval_recursively(self, path=None):
        """
        Evaluates the decision tree from this node based on the given state.
        Returns the final action and records the decision path.
        """
        if path is None:
            path = []
        path.append(self)

        value = self.eval()

        self._post_eval()

        for condition, child in self.nodes.items():
            LOGGER.warning(f'{self.__class__.__name__} should not have any sub-nodes.')
            if condition == value or condition is NO_CONDITION:
                return child.eval_recursively(path=path)

        return value, path

    def append(self, expression: Self, edge_condition: Any = None):
        raise TooManyChildren("Cannot append child to an ActionNode!")
