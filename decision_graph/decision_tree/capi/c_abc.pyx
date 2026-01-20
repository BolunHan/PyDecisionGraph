import linecache
import operator
import sys
import traceback
import uuid
import warnings

from cpython.mem cimport PyMem_Calloc, PyMem_Free
from cpython.pystate cimport PyThreadState_Get
from cpython.ref cimport Py_INCREF, Py_DECREF
from cython import final
from libc.stdint cimport uintptr_t

from .. import LOGGER
from ..exc import *

LOGGER = LOGGER.getChild('abc')

cdef dict GLOBAL_SINGLETON = {}


cdef class Singleton:
    def __cinit__(self):
        cdef tuple reg_key = (self.__class__.__module__, self.__class__.__qualname__)
        if reg_key in GLOBAL_SINGLETON:
            raise RuntimeError(f'Can not initialize new singleton {self.__class__}')
        else:
            GLOBAL_SINGLETON[reg_key] = self


cdef class NodeEdgeCondition(Singleton):
    def __cinit__(self, object value=None):
        if value is not None:
            Py_INCREF(value)
            self.value_addr = <PyObject*> value
        else:
            self.value_addr = NULL

    def __hash__(self):
        return (<object> self.value_addr).__hash__()

    def __repr__(self):
        return f'<{self.__class__.__name__} {<uintptr_t> <PyObject*> self:#0x}>'

    property value:
        def __get__(self):
            if not self.value_addr:
                raise ValueError("Condition has no value assigned.")
            return <object> self.value_addr

        def __set__(self, value):
            Py_INCREF(self.value)
            if self.value_addr is not NULL:
                Py_DECREF(<object> self.value_addr)
            self.value_addr = <PyObject*> value


cdef class ConditionElse(NodeEdgeCondition):
    def __cinit__(self, *args, **kwargs):
        self.value_addr = NULL

    def __hash__(self):
        return <uintptr_t> <PyObject*> self

    def __repr__(self):
        return f'<CONDITION Internal {<uintptr_t> <PyObject*> self:#0x}>(Else)'

    def __str__(self):
        return 'Else'

    property value:
        def __set__(self, value):
            raise NotImplementedError()


cdef class ConditionAny(NodeEdgeCondition):
    def __cinit__(self, *args, **kwargs):
        self.value_addr = NULL

    def __hash__(self):
        return <uintptr_t> <PyObject*> self

    def __repr__(self):
        return f'<CONDITION {<uintptr_t> <PyObject*> self:#0x}>(Unconditional)'

    def __str__(self):
        return 'Unconditional'

    property value:
        def __set__(self, value):
            raise NotImplementedError()


cdef class ConditionAuto(NodeEdgeCondition):
    def __cinit__(self, *args, **kwargs):
        self.value_addr = NULL

    def __hash__(self):
        raise NotImplementedError()

    def __repr__(self):
        return f'<CONDITION Internal {<uintptr_t> <PyObject*> self:#0x}>(AutoInfer)'

    def __str__(self):
        return 'AutoInfer'


cdef class BinaryCondition(NodeEdgeCondition):
    def __cinit__(self, *args, **kwargs):
        self.value_addr = NULL

    def __hash__(self):
        return <uintptr_t> <PyObject*> self

    property value:
        def __set__(self, value):
            raise NotImplementedError()


cdef class ConditionTrue(BinaryCondition):
    def __repr__(self):
        return f'<CONDITION {<uintptr_t> <PyObject*> self:#0x}>(True)'

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

    property value:
        def __get__(self):
            return True


cdef class ConditionFalse(BinaryCondition):
    def __repr__(self):
        return f'<CONDITION {<uintptr_t> <PyObject*> self:#0x}>(False)'

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

    property value:
        def __get__(self):
            return False


cdef ConditionAny NO_CONDITION = ConditionAny()
cdef ConditionElse ELSE_CONDITION = ConditionElse()
cdef ConditionAuto AUTO_CONDITION = ConditionAuto()
cdef ConditionTrue TRUE_CONDITION = ConditionTrue()
cdef ConditionFalse FALSE_CONDITION = ConditionFalse()


globals().update(
    NO_CONDITION=NO_CONDITION,
    ELSE_CONDITION=ELSE_CONDITION,
    AUTO_CONDITION=AUTO_CONDITION,
    TRUE_CONDITION=TRUE_CONDITION,
    FALSE_CONDITION=FALSE_CONDITION
)


cdef class SkipContextsBlock:
    def __cinit__(self):
        self.skip_exception = type(f"{self.__class__.__name__}SkipException", (EmptyBlock,), {"owner": self})
        self.tracer_override = False
        self.default_entry_check = True

    cdef bint c_entry_check(self):
        return self.default_entry_check

    cdef void c_on_enter(self):
        pass

    cdef void c_on_exit(self):
        pass

    # === Python Interfaces ===
    @final
    def __enter__(self):
        if self.c_entry_check():  # Check if the expression evaluates to True
            self.c_on_enter()
            return self

        cdef PyThreadState* tstate = PyThreadState_Get()
        PyThreadState_EnterTracing(tstate)

        self.cframe = sys._getframe()
        self.enter_line = (self.cframe.f_code.co_filename, self.cframe.f_lineno)

        self.cframe_tracer_sig_count = 0
        self.cframe_tracer = self.cframe.f_trace
        self.cframe.f_trace = self.cframe_tracer_skipper
        if self.cframe_tracer is not None:
            warnings.warn('Not supporting a custom tracer, must clear it before passing in.')

        self.global_tracer_sig_count = 0
        self.global_tracer = sys.gettrace()
        sys.settrace(self.global_tracer_skipper)

        self.global_profiler_sig_count = 0
        self.global_profiler = sys.getprofile()
        sys.setprofile(self.global_profile_tracer)

        self.tracer_override = True
        PyThreadState_LeaveTracing(tstate)
        return self

    @final
    def __exit__(self, exc_type, exc_value, exc_traceback):
        self.restore_tracers(True)

        if exc_type is None:
            self.c_on_exit()
            return None

        if issubclass(exc_type, self.skip_exception):
            # in this case, the block is not even entered, so no need to call c_on_exit cleanup.
            return True

        self.c_on_exit()
        return False

    def restore_tracers(self, override=False):
        if self.tracer_override:
            print('[restore_tracers] restoring tracers...')
            if self.cframe_tracer is not None:
                print('[restore_tracers] restoring cframe tracer:', self.cframe_tracer)
                self.cframe.f_trace = self.cframe_tracer
            else:
                self.cframe.f_trace = None

            if self.global_profiler is not None:
                print('[restore_tracers] restoring global profiler:', self.global_profiler)
                sys.setprofile(self.global_profiler)
            else:
                sys.setprofile(None)

            if self.global_tracer is not None:
                print('[restore_tracers] restoring global tracer:', self.global_tracer)
                sys.settrace(self.global_tracer)
            else:
                sys.settrace(None)

            self.tracer_override = False

    def cframe_tracer_skipper(self, frame, event, arg):
        print(f'[cframe_tracer_skipper] sig {self.cframe_tracer_sig_count}...', frame, event, arg)
        cdef PyThreadState* tstate
        cdef str line
        self.cframe_tracer_sig_count += 1
        if event == 'line':
            line = linecache.getline(frame.f_code.co_filename, frame.f_lineno).strip()
            print('[cframe_tracer_skipper] line:', line)
            if line.startswith(('pass', '...')):
                return self.cframe_tracer_skipper
            elif self.enter_line == (frame.f_code.co_filename, frame.f_lineno):
                tstate = PyThreadState_Get()
                PyThreadState_EnterTracing(tstate)
                self.restore_tracers()
                PyThreadState_LeaveTracing(tstate)
                return self.cframe_tracer_skipper
            tstate = PyThreadState_Get()
            PyThreadState_EnterTracing(tstate)
            self.restore_tracers()
            PyThreadState_LeaveTracing(tstate)
            raise self.skip_exception('')
        return self.cframe_tracer_skipper

    def global_tracer_skipper(self, frame, event, arg):
        print(f'[global_tracer_skipper] sig {self.global_tracer_sig_count}...', frame, event, arg)
        self.global_tracer_sig_count += 1
        return self.global_tracer_skipper

    def global_profile_tracer(self, frame, event, arg):
        print(f'[global_profile_tracer] sig {self.global_profiler_sig_count}...', frame, event, arg)
        cdef PyThreadState* tstate
        self.global_profiler_sig_count += 1
        if event == 'c_call':
            tstate = PyThreadState_Get()
            PyThreadState_EnterTracing(tstate)
            self.restore_tracers()
            PyThreadState_LeaveTracing(tstate)
            raise self.skip_exception('')
        return self.global_profile_tracer


cdef class LogicExpression(SkipContextsBlock):
    def __cinit__(self, *, object expression=None, type dtype=None, str repr=None, object uid=None, **kwargs):
        self.expression = expression
        self.dtype = dtype
        self.repr = repr if repr is not None else str(expression)
        self.uid = uuid.uuid4() if uid is None else uid

    cdef bint c_entry_check(self):
        return bool(self.c_eval(False))

    cdef object c_eval(self, bint enforce_dtype):
        if isinstance(self.expression, (float, int, bool, str)):
            value = self.expression
        elif callable(self.expression):
            value = self.expression()
        elif isinstance(self.expression, Exception):
            raise self.expression
        else:
            raise TypeError(f"Unsupported expression type: {type(self.expression)}.")

        if self.dtype is None:
            pass  # No type enforcement
        elif enforce_dtype:
            value = self.dtype(value)
        elif not isinstance(value, self.dtype):
            LOGGER.warning(f"Evaluated value {value} does not match dtype {self.dtype.__name__}.")

        return value

    @staticmethod
    cdef LogicExpression c_cast(object value, type dtype):
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
                dtype=dtype,
                repr=f"Eval({value})"
            )
        if isinstance(value, Exception):
            return LogicExpression(
                expression=value,
                dtype=dtype,
                repr=f"Raises({type(value).__name__}: {value})"
            )
        raise TypeError(f"Unsupported type for LogicExpression conversion: {type(value)}.")

    @staticmethod
    cdef LogicExpression c_math_op(LogicExpression self, object other, object op, str operator_str, type dtype):
        other_expr = LogicExpression.cast(other)

        if dtype is None:
            dtype = self.dtype

        new_expr = LogicExpression(
            expression=lambda: op(self.eval(), other_expr.eval()),
            dtype=dtype,
            repr=f"({self.repr} {operator_str} {other_expr.repr})",
        )
        return new_expr

    # === Python Interface ===

    def eval(self, enforce_dtype=False):
        return self.c_eval(enforce_dtype)

    @classmethod
    def cast(cls, object value, type dtype=None):
        return LogicExpression.c_cast(value, dtype)

    def __bool__(self) -> bool:
        return bool(self.eval())

    def __and__(self, object other) -> LogicExpression:
        other_expr = self.cast(value=other, dtype=bool)
        new_expr = LogicExpression(
            expression=lambda: self.eval() and other_expr.eval(),
            dtype=bool,
            repr=f"({self.repr} and {other_expr.repr})"
        )
        return new_expr

    def __eq__(self, object other) -> LogicExpression:
        if isinstance(other, LogicExpression):
            other_value = other.eval()
        else:
            other_value = other

        return LogicExpression(
            expression=lambda: self.eval() == other_value,
            dtype=bool,
            repr=f"({self.repr} == {repr(other_value)})"
        )

    def __or__(self, object other) -> LogicExpression:
        other_expr = self.cast(value=other, dtype=bool)
        new_expr = LogicExpression(
            expression=lambda: self.eval() or other_expr.eval(),
            dtype=bool,
            repr=f"({self.repr} or {other_expr.repr})"
        )
        return new_expr

    # Math operators
    def __add__(self, object other):
        return LogicExpression.c_math_op(self, other, operator.add, "+", None)

    def __sub__(self, object other):
        return LogicExpression.c_math_op(self, other, operator.sub, "-", None)

    def __mul__(self, object other):
        return LogicExpression.c_math_op(self, other, operator.mul, "*", None)

    def __truediv__(self, object other):
        return LogicExpression.c_math_op(self, other, operator.truediv, "/", None)

    def __floordiv__(self, object other):
        return LogicExpression.c_math_op(self, other, operator.floordiv, "//", None)

    def __pow__(self, object other):
        return LogicExpression.c_math_op(self, other, operator.pow, "**", None)

    # Comparison operators, note that __eq__, __ne__ is special and should not implement as math operator
    def __lt__(self, object other):
        return LogicExpression.c_math_op(self, other, operator.lt, "<", bool)

    def __le__(self, object other):
        return LogicExpression.c_math_op(self, other, operator.le, "<=", bool)

    def __gt__(self, object other):
        return LogicExpression.c_math_op(self, other, operator.gt, ">", bool)

    def __ge__(self, object other):
        return LogicExpression.c_math_op(self, other, operator.ge, ">=", bool)

    def __repr__(self) -> str:
        return f"<{self.__class__.__name__}>(dtype={'Any' if self.dtype is None else self.dtype.__name__}, repr={self.repr})"


cdef class LogicGroupManager(Singleton):
    def __cinit__(self):
        self._cache = {}
        self._active_groups = <LogicGroupStack*> PyMem_Calloc(1, sizeof(LogicGroupStack))
        self._active_nodes = <LogicNodeStack*> PyMem_Calloc(1, sizeof(LogicNodeStack))
        self._breakpoint_nodes = <LogicNodeStack*> PyMem_Calloc(1, sizeof(LogicNodeStack))
        self._shelved_state = <ShelvedStateStack*> PyMem_Calloc(1, sizeof(ShelvedStateStack))

        self.inspection_mode = False  # run node graph in inspection mode, without evaluating value, to map the graph
        self.vigilant_mode = False  # disable auto generation of missing action nodes

    def __dealloc__(self):
        if self._active_groups:
            while self._active_groups.size:
                self.c_lg_exit()
            PyMem_Free(self._active_groups)

        if self._active_nodes:
            while self._active_nodes.size:
                LogicGroupManager.c_ln_stack_pop(self._active_nodes)
            PyMem_Free(self._active_nodes)

        if self._breakpoint_nodes:
            while self._breakpoint_nodes.size:
                LogicGroupManager.c_ln_stack_pop(self._breakpoint_nodes)
            PyMem_Free(self._breakpoint_nodes)

        if self._shelved_state:
            while self._shelved_state.size:
                self.c_unshelve()
            PyMem_Free(self._shelved_state)

    @staticmethod
    cdef inline void c_ln_stack_push(LogicNodeStack* stack, LogicNode logic_node):
        cdef LogicNodeFrame* frame = <LogicNodeFrame*> PyMem_Calloc(1, sizeof(LogicNodeFrame))
        frame.logic_node = <PyObject*> logic_node
        Py_INCREF(logic_node)
        frame.prev = stack.top
        stack.top = frame
        stack.size += 1

    @staticmethod
    cdef inline void c_ln_stack_pop(LogicNodeStack* stack, LogicNode logic_node=None):
        if not stack.top:
            raise RuntimeError("No active LogicNode")
        cdef LogicNodeFrame* frame = stack.top
        cdef PyObject* ln = NULL if logic_node is None else <PyObject*> logic_node

        if ln and frame.logic_node != ln:
            raise AssertionError("The LogicNode is not currently active.")

        stack.top = frame.prev
        stack.size -= 1
        Py_DECREF(<object> frame.logic_node)
        PyMem_Free(frame)

    @staticmethod
    cdef inline void c_ln_stack_remove(LogicNodeStack* stack, LogicNode logic_node):
        cdef PyObject* target = <PyObject*> logic_node
        cdef LogicNodeFrame* prev = NULL
        cdef LogicNodeFrame* curc = stack.top

        # Traverse the stack linked list
        while curc is not NULL:
            if curc.logic_node == target:
                # Found. Unlink it.
                if prev:
                    prev.prev = curc.prev
                else:
                    stack.top = curc.prev

                stack.size -= 1

                Py_DECREF(<object> curc.logic_node)
                PyMem_Free(curc)
                return

            prev = curc
            curc = curc.prev

        # Not found
        raise NodeNotFountError("LogicNode not found in stack")

    @staticmethod
    cdef inline LogicNodeFrame* c_ln_stack_locate(LogicNodeStack* stack, LogicNode logic_node):
        cdef PyObject* target = <PyObject*> logic_node
        cdef LogicNodeFrame* frame = stack.top

        # Traverse the stack linked list
        while frame:
            if frame.logic_node == target:
                return frame
            frame = frame.prev
        return NULL

    @staticmethod
    cdef inline void c_ln_stack_relocate(LogicNodeStack* original_stack, LogicNodeStack* new_stack, LogicNode logic_node):
        cdef PyObject* target = <PyObject*> logic_node
        cdef LogicNodeFrame* prev = NULL
        cdef LogicNodeFrame* frame = original_stack.top

        # Locate the frame in original_stack
        while frame:
            if frame.logic_node == target:
                # Found: unlink from original_stack
                if prev is NULL:
                    # Frame is at the top
                    original_stack.top = frame.prev
                else:
                    prev.prev = frame.prev

                original_stack.size -= 1

                # Push onto new_stack by relinking
                frame.prev = new_stack.top
                new_stack.top = frame
                new_stack.size += 1
                return

            prev = frame
            frame = frame.prev
        raise NodeNotFountError("LogicNode not found in original stack")

    cdef inline LogicGroup c_cached_init(self, str name, type cls, dict kwargs):
        cdef tuple reg_key = (cls.__module__, cls.__qualname__)
        cdef dict registry
        if reg_key in self._cache:
            registry = self._cache[reg_key]
        else:
            registry = self._cache[reg_key] = {}

        if name in registry:
            return registry[name]

        cdef LogicGroup logic_group = cls(name=name, **kwargs)
        registry[name] = logic_group
        return logic_group

    cdef inline void c_lg_enter(self, LogicGroup logic_group):
        # Step 1: Update parent info
        cdef LogicGroupFrame* frame = self._active_groups.top
        if frame:
            logic_group.parent = <LogicGroup> <object> frame.logic_group

        # Step 2: Push to active_groups stack
        frame = <LogicGroupFrame*> PyMem_Calloc(1, sizeof(LogicGroupFrame))
        frame.logic_group = <PyObject*> logic_group
        Py_INCREF(logic_group)
        frame.prev = self._active_groups.top
        self._active_groups.top = frame
        self._active_groups.size += 1

    cdef inline void c_lg_exit(self, LogicGroup logic_group=None):
        cdef LogicGroupFrame* frame = self._active_groups.top

        if not frame:
            raise RuntimeError("No active LogicGroup")

        # Step 1: Validate logic group if provided
        if logic_group is not None:
            if frame.logic_group != <PyObject*> logic_group:
                raise AssertionError("The LogicGroup is not currently active.")
        else:
            logic_group = <LogicGroup> <object> frame.logic_group

        # Step 2: Activate pending breakpoints
        cdef LogicNodeFrame* breakpoint_frame = self._breakpoint_nodes.top
        cdef BreakpointNode breakpoint_node
        while breakpoint_frame:
            breakpoint_node = <BreakpointNode> <object> breakpoint_frame.logic_node
            if breakpoint_node.break_from is logic_group:
                breakpoint_node.await_connection = True
            breakpoint_frame = breakpoint_frame.prev

        # Step 3: Pop logic group from active stack
        self._active_groups.top = frame.prev
        self._active_groups.size -= 1
        Py_DECREF(logic_group)
        PyMem_Free(frame)

    cdef inline void c_ln_enter(self, LogicNode logic_node):
        # Step 1: Filter action node
        if isinstance(logic_node, ActionNode):
            LOGGER.error('Enter the with code block of an ActionNode rejected. Check is this intentional?')
            return

        # Step 2: Connect to breakpoints
        cdef LogicNodeFrame* breakpoint_frame = self._breakpoint_nodes.top
        cdef LogicNodeFrame* breakpoint_frame_next
        cdef LogicNodeFrame* breakpoint_frame_prev = NULL
        cdef BreakpointNode breakpoint_node
        while breakpoint_frame:
            breakpoint_frame_next = breakpoint_frame.prev
            breakpoint_node = <BreakpointNode> <object> breakpoint_frame.logic_node
            if breakpoint_node.await_connection:
                # connect the breakpoint with current node
                breakpoint_node.c_connect(logic_node)
                # unlink from breakpoint stack
                if breakpoint_frame_prev:
                    breakpoint_frame_prev.prev = breakpoint_frame_next
                else:
                    self._breakpoint_nodes.top = breakpoint_frame_next
                self._breakpoint_nodes.size -= 1
                Py_DECREF(<object> breakpoint_node)
                PyMem_Free(breakpoint_frame)
                # will NOT update breakpoint_frame_prev
                breakpoint_frame = breakpoint_frame_next
            else:
                breakpoint_frame_prev = breakpoint_frame
                breakpoint_frame = breakpoint_frame_next

        # Step 3: Get current active node
        cdef LogicNodeFrame* active_frame = self._active_nodes.top

        # Step 3.1: First active node, push to stack directly
        if not active_frame:
            LogicGroupManager.c_ln_stack_push(self._active_nodes, logic_node)
            return

        # Step 4: Locate first placeholder
        cdef LogicNode active_node = <LogicNode> <object> active_frame.logic_node
        cdef PlaceholderNode placeholder = active_node.c_get_placeholder()

        # Step 5: Replace placeholder
        active_node.c_replace(placeholder, logic_node)

        # Step 6: Push self to active stack
        LogicGroupManager.c_ln_stack_push(self._active_nodes, logic_node)

    cdef inline void c_ln_exit(self, LogicNode logic_node):
        LogicGroupManager.c_ln_stack_pop(self._active_nodes, logic_node)

    cdef inline void c_shelve(self):
        cdef ShelvedStateFrame* frame = <ShelvedStateFrame*> PyMem_Calloc(1, sizeof(ShelvedStateFrame))
        frame.active_groups = self._active_groups
        frame.active_nodes = self._active_nodes
        frame.breakpoint_nodes = self._breakpoint_nodes
        frame.inspection_mode = self.inspection_mode
        frame.vigilant_mode = self.vigilant_mode

        frame.prev = self._shelved_state.top
        self._shelved_state.top = frame
        self._shelved_state.size += 1

        self._active_groups = <LogicGroupStack*> PyMem_Calloc(1, sizeof(LogicGroupStack))
        self._active_nodes = <LogicNodeStack*> PyMem_Calloc(1, sizeof(LogicNodeStack))
        self._breakpoint_nodes = <LogicNodeStack*> PyMem_Calloc(1, sizeof(LogicNodeStack))

    cdef inline void c_unshelve(self):
        if not self._shelved_state.top:
            raise RuntimeError("No shelved state to unshelve.")

        cdef ShelvedStateFrame* frame = self._shelved_state.top
        cdef LogicGroupStack* active_groups = frame.active_groups
        cdef LogicNodeStack* active_nodes = frame.active_nodes
        cdef LogicNodeStack* breakpoint_nodes = frame.breakpoint_nodes
        cdef bint inspection_mode = frame.inspection_mode
        cdef bint vigilant_mode = frame.vigilant_mode

        self._shelved_state.top = frame.prev
        self._shelved_state.size -= 1
        PyMem_Free(frame)

        self.c_clear()
        self._active_groups = active_groups
        self._active_nodes = active_nodes
        self._breakpoint_nodes = breakpoint_nodes
        self.inspection_mode = inspection_mode
        self.vigilant_mode = vigilant_mode

    cdef inline void c_clear(self):
        if self._active_groups:
            while self._active_groups.size:
                self.c_lg_exit()
            PyMem_Free(self._active_groups)
            self._active_groups = NULL

        if self._active_nodes:
            while self._active_nodes.size:
                LogicGroupManager.c_ln_stack_pop(self._active_nodes)
            PyMem_Free(self._active_nodes)
            self._active_nodes = NULL

        if self._breakpoint_nodes:
            while self._breakpoint_nodes.size:
                LogicGroupManager.c_ln_stack_pop(self._breakpoint_nodes)
            PyMem_Free(self._breakpoint_nodes)
            self._breakpoint_nodes = NULL

    def __call__(self, str name, type cls, **kwargs) -> LogicGroup:
        return self.c_cached_init(name, cls, kwargs)

    def __contains__(self, LogicGroup instance) -> bool:
        cdef type cls = instance.__class__
        cdef str name = instance.name
        cdef tuple reg_key = (cls.__module__, cls.__qualname__)
        if reg_key not in self._cache:
            return False
        cdef dict registry = self._cache[reg_key]
        return name in registry

    def shelve(self):
        self.c_shelve()

    def unshelve(self):
        self.c_unshelve()

    def clear(self):
        self._cache.clear()
        self.c_clear()

        self._active_groups = <LogicGroupStack*> PyMem_Calloc(1, sizeof(LogicGroupStack))
        self._active_nodes = <LogicNodeStack*> PyMem_Calloc(1, sizeof(LogicNodeStack))
        self._breakpoint_nodes = <LogicNodeStack*> PyMem_Calloc(1, sizeof(LogicNodeStack))

    property active_group:
        def __get__(self):
            if not self._active_groups.top:
                return None

            cdef LogicGroupFrame* frame = self._active_groups.top
            cdef PyObject* lg = frame.logic_group
            return <LogicGroup> <object> lg

    property active_node:
        def __get__(self):
            if not self._active_nodes.top:
                return None
            cdef LogicNodeFrame* frame = self._active_nodes.top
            cdef PyObject* ln = frame.logic_node
            return <LogicNode> <object> ln


cdef LogicGroupManager LGM = LogicGroupManager()
globals()['LGM'] = LGM


cdef class LogicGroup:
    def __cinit__(self, *, str name=None, LogicGroup parent=None, dict contexts=None, **kwargs):
        self.name = f"{self.__class__.__name__}.{uuid.uuid4()}" if name is None else name

        if self in LGM:
            raise RuntimeError(f"LogicGroup {name} of type {self.__class__.__name__} already exists!")

        self.parent = parent
        self.Break = type(f"{self.__class__.__name__}Break", (BreakBlock,), {})
        self.contexts = {} if contexts is None else contexts

    cdef void c_break_inspection(self):
        cdef LogicNodeFrame* frame = LGM._active_nodes.top

        # Case 1: No active node, breaks affect nothing
        if not frame:
            return

        cdef LogicNode active_node = <LogicNode> <object> frame.logic_node
        # Case 2: Active node located, traverse its children to find placeholder to mark as breakpoint
        # will not break from scope in inspection mode

        # Step 2.1: Locating the placeholder node for breakpoint
        cdef PlaceholderNode placeholder = active_node.c_get_placeholder()
        cdef BreakpointNode breakpoint_node = BreakpointNode(break_from=self)
        active_node.c_replace(placeholder, breakpoint_node)
        # Step 2.2: Push the breakpoint node to the global breakpoint stack
        LogicGroupManager.c_ln_stack_push(LGM._breakpoint_nodes, breakpoint_node)

    cdef void c_break_active(self):
        cdef LogicGroupFrame* frame = LGM._active_groups.top
        if not frame:
            raise RuntimeError("No active LogicGroup to break from.")
        cdef PyObject* active_node = frame.logic_group
        if active_node != <PyObject*> self:
            raise IndexError('Not breaking from the top active LogicGroup.')
        raise self.Break()

    cdef void c_break_runtime(self):
        if not LGM._active_nodes.size:
            raise RuntimeError("No active LogicGroup to break from.")

        cdef PyObject* addr_self = <PyObject*> self
        cdef LogicGroupFrame* frame = LGM._active_groups.top
        cdef bint found = False

        # step 1: Validate that the break scope is in the active stack
        while frame:
            if frame.logic_group == addr_self:
                found = True
                break
            frame = frame.prev

        if not found:
            raise ValueError(f"Break scope {self} not in active LogicGroup stack.")

        # step 2: recursive breaking from top of the stack
        frame = LGM._active_groups.top
        cdef LogicGroup active_group
        while frame:
            active_group = <LogicGroup> <object> frame.logic_group
            active_group.c_break_active()
            if active_group is self:
                break
            frame = frame.prev

    # === Python Interfaces ===

    def __repr__(self):
        return f'<{self.__class__.__name__}>({self.name!r})'

    def __enter__(self):
        LGM.c_lg_enter(self)
        return self

    def __exit__(self, exc_type, exc_value, exc_traceback):
        LGM.c_lg_exit(self)

        if exc_type is None:
            return None

        if issubclass(exc_type, self.Break):
            return True
        return False

    @classmethod
    def break_(cls, LogicGroup scope=None):
        if scope is None:
            scope = LGM.active_group

        if scope is None:
            raise RuntimeError("No active LogicGroup to break from.")

        if LGM.inspection_mode:
            scope.c_break_inspection()
        else:
            scope.c_break_runtime()

    def break_active(self):
        self.c_break_active()

    def break_inspection(self):
        self.c_break_inspection()

    def break_runtime(self):
        self.c_break_runtime()


cdef class LogicNode(LogicExpression):
    def __cinit__(self, *, **kwargs):
        self.subordinates = <LogicNodeStack*> PyMem_Calloc(1, sizeof(LogicNodeStack))
        self.condition_to_parent = NO_CONDITION
        self.parent = None
        self.children = {}
        self.labels = []
        self.autogen = False

        # update labels from active groups
        cdef LogicGroupFrame* frame = LGM._active_groups.top
        cdef LogicGroup lg
        while frame:
            lg = <LogicGroup> <object> frame.logic_group
            self.labels.append(lg.name)
            frame = frame.prev

    def __dealloc__(self):
        if self.subordinates:
            while self.subordinates.size:
                LogicGroupManager.c_ln_stack_pop(self.subordinates)
            PyMem_Free(self.subordinates)

    cdef NodeEdgeCondition c_infer_condition(self, LogicNode child):
        # infer condition based on registered children
        cdef LogicNodeStack* stack = self.subordinates
        cdef LogicNodeFrame* frame = stack.top

        cdef size_t size = stack.size

        # Case 1: No child node registered, the first child is always TRUE unless specified
        if size == 0:
            return TRUE_CONDITION

        cdef LogicNode last_node = <LogicNode> <object> frame.logic_node
        cdef NodeEdgeCondition last_condition = last_node.condition_to_parent

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

        cdef LogicNode second_node = <LogicNode> <object> frame.prev.logic_node
        cdef NodeEdgeCondition second_condition = second_node.condition_to_parent
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

    cdef PlaceholderNode c_get_placeholder(self):
        cdef LogicNodeFrame* frame = self.subordinates.top
        cdef PlaceholderNode placeholder

        # Case 1: No child node registered, return a TRUE placeholder
        if not frame:
            placeholder = PlaceholderNode(auto_connect=False)
            self.c_append(placeholder, TRUE_CONDITION)
            return placeholder

        # Case 2: Traverse the stack to find existing placeholder
        cdef LogicNode node
        while frame:
            node = <LogicNode> <object> frame.logic_node
            if isinstance(node, PlaceholderNode):
                return <PlaceholderNode> node
            frame = frame.prev

        # Case 3: No existing placeholder, create a new one with AUTO_CONDITION.
        placeholder = PlaceholderNode(auto_connect=False)
        # c_append will validate and infer the condition, and raise error as we intended.
        self.c_append(placeholder, AUTO_CONDITION)
        return placeholder

    cdef void c_append(self, LogicNode child, NodeEdgeCondition condition):
        if condition is None:
            raise ValueError("LogicNode must have an valid edge condition.")

        if condition is AUTO_CONDITION:
            condition = self.c_infer_condition(child)

        if condition in self.children:
            raise KeyError(f"Edge {condition} already registered.")

        self.children[condition] = child
        LogicGroupManager.c_ln_stack_push(self.subordinates, child)
        child.parent = self
        child.condition_to_parent = condition

    cdef void c_overwrite(self, LogicNode new_node, NodeEdgeCondition condition):
        if condition is None:
            raise ValueError("LogicNode must have an valid edge condition.")

        if condition is AUTO_CONDITION:
            condition = self.c_infer_condition(new_node)

        if condition not in self.children:
            raise KeyError(f"Edge {condition} not registered, cannot overwrite.")

        cdef LogicNode original_node = self.children[condition]
        self.children[condition] = new_node
        new_node.parent = self
        new_node.condition_to_parent = condition

        cdef LogicNodeFrame* frame = LogicGroupManager.c_ln_stack_locate(self.subordinates, original_node)
        if not frame:
            raise LookupError(f'Failed to locate {original_node} from subordinates, buffer corruption detected.')

        Py_DECREF(<object> frame.logic_node)
        frame.logic_node = <PyObject*> new_node
        Py_INCREF(<object> frame.logic_node)
        original_node.parent = None
        original_node.condition_to_parent = NO_CONDITION

    cdef void c_replace(self, LogicNode original_node, LogicNode new_node):
        cdef NodeEdgeCondition condition
        cdef LogicNode child
        cdef LogicNodeFrame* frame

        # Step 1: Safety check:
        frame = LogicGroupManager.c_ln_stack_locate(LGM._active_nodes, original_node)
        if frame:
            raise RuntimeError('Must not replace active node. Existing first required.')

        # Step 2: Locate original node from stack
        frame = LogicGroupManager.c_ln_stack_locate(self.subordinates, original_node)
        if not frame:
            raise NodeNotFountError(f'Failed to locate {original_node} from subordinates.')

        self.children[original_node.condition_to_parent] = new_node
        new_node.parent = self
        new_node.condition_to_parent = original_node.condition_to_parent

        Py_DECREF(<object> frame.logic_node)
        frame.logic_node = <PyObject*> new_node
        Py_INCREF(<object> frame.logic_node)

        original_node.parent = None
        original_node.condition_to_parent = NO_CONDITION

    cdef void c_validate(self):
        cdef size_t size = self.subordinates.size
        if size != <size_t> len(self.children):
            raise NodeValueError('Subordinate stack size does not match registered children.')

        cdef LogicNode child
        cdef NodeEdgeCondition condition
        cdef LogicNodeFrame* frame
        for condition, child in self.children.items():
            if child.condition_to_parent is not condition:
                raise EdgeValueError('Child node condition does not match registered condition.')
            frame = LogicGroupManager.c_ln_stack_locate(self.subordinates, child)
            # in the locating function the existence is already checked
            # but add this for a clearer error message
            if not frame:
                raise ValueError(f"LogicNode {child} not found in stack")

    cdef tuple c_eval_recursively(self, list path=None, object default=NO_DEFAULT):
        if path is None:
            path = []
        else:
            path.append(self)

        cdef object value
        if LGM.vigilant_mode:
            try:
                value = self.c_eval(False)
            except Exception as e:
                raise ExpressEvaluationError(f"Failed to evaluate {self}, {traceback.format_exc()}") from e
        else:
            value = self.c_eval(False)

        if self.is_leaf:
            return value, path

        cdef LogicNode else_branch = None
        cdef LogicNodeFrame* frame = self.subordinates.top
        cdef LogicNode child
        cdef NodeEdgeCondition condition

        while frame:
            child = <LogicNode> <object> frame.logic_node
            condition = child.condition_to_parent
            if condition is ELSE_CONDITION:
                else_branch = child
            elif condition is NO_CONDITION or value == condition.value:
                return child.c_eval_recursively(path, default)
            frame = frame.prev

        if else_branch is not None:
            return else_branch.c_eval_recursively(path, default)

        if default is NO_DEFAULT:
            raise ValueError(f"No matching condition found for value {value} at '{self.repr}'.")

        LOGGER.warning(f"No matching condition found for value {value} at '{self.repr}', using default {default}.")
        return default, path

    cdef void c_auto_fill(self):
        cdef size_t size = len(self.children)
        cdef LogicNode no_action = NoAction(auto_connect=False, autogen=True)

        # Case 1: No child node registered
        if size == 0:
            LOGGER.warning(f"{self} having no [True] branch. Check the <with> statement code block to see if this is intended.")
            self.c_append(no_action, NO_CONDITION)
            return

        cdef LogicNodeFrame* frame = self.subordinates.top
        cdef LogicNode node = <LogicNode> <object> frame.logic_node
        cdef NodeEdgeCondition condition = node.condition_to_parent

        # Case 2: Signal node
        if size == 1:
            # Case 2.1: single child with no condition
            if condition is NO_CONDITION:
                return

            # Case 2.2: single child deliberately set to ELSE, which indicate it is expecting a true branch
            if condition is ELSE_CONDITION:
                LOGGER.warning(f"{self} having no [True] branch. Check the <with> statement code block to see if this is intended.")
                self.c_overwrite(no_action, TRUE_CONDITION)
                return

            # Case 2.3: no need to check for auto condition, as it is not hashable

            # Case 2.4: single child with binary condition
            if isinstance(condition, BinaryCondition):
                self.c_append(no_action, ~condition)
                return

        cdef LogicNode second_node = <LogicNode> <object> frame.prev.logic_node
        cdef NodeEdgeCondition second_condition = second_node.condition_to_parent
        # Case 3: Double Node
        if size == 2:
            # Case 3.1: if one of the branch is unconditioned, raise
            if condition is NO_CONDITION or second_condition is NO_CONDITION:
                raise TooManyChildren('Cannot have unconditioned branch when there are two branches.')

            # Case3.2: if one of the branch is ELSE, and the other is any valid condition except ELSE too, all good
            # Since we already validate node, there is no same registered condition
            if condition is ELSE_CONDITION or second_condition is ELSE_CONDITION:
                return

            # Case 3.3: both conditions are binary, all good
            if isinstance(condition, BinaryCondition) and isinstance(second_condition, BinaryCondition):
                return

            # Case 3.4: all conditions are non-binary, add a protective else branch
            if not isinstance(condition, BinaryCondition) and not isinstance(second_condition, BinaryCondition):
                self.c_append(no_action, ELSE_CONDITION)
                return

            raise EdgeValueError(f'Conflicting conditions detected, {condition} and {second_condition}.')

        # Case 3: Multiple nodes, it must be of all generic conditions
        cdef bint else_detected = False
        for condition in self.children:
            if condition is NO_CONDITION:
                raise TooManyChildren('Cannot have unconditioned branch when there are multiple branches.')

            if condition is ELSE_CONDITION:
                else_detected = True

            if isinstance(condition, BinaryCondition):
                raise TooManyChildren('Cannot have binary branch when there are more than 3 branches.')

        if not else_detected:
            self.c_append(no_action, ELSE_CONDITION)

    cdef size_t c_consolidate_placeholder(self):
        cdef LogicNodeFrame* frame = self.subordinates.top
        cdef LogicNode node
        cdef size_t placeholder_count = 0

        while frame:
            node = <object> frame.logic_node
            if isinstance(node, PlaceholderNode):
                self.c_replace(node, NoAction(auto_connect=False, autogen=True))
                placeholder_count += 1
            frame = frame.prev
        return placeholder_count

    cdef bint c_entry_check(self):
        if LGM.inspection_mode:
            return True
        return bool(self.c_eval(False))

    cdef void c_on_enter(self):
        # Placeholders must be in reversed order, so that locating placeholder works correctly.
        self.c_append(PlaceholderNode(auto_connect=False), FALSE_CONDITION)
        self.c_append(PlaceholderNode(auto_connect=False), TRUE_CONDITION)
        LGM.c_ln_enter(self)

    cdef void c_on_exit(self):
        self.c_validate()
        self.c_auto_fill()
        self.c_consolidate_placeholder()
        LGM.c_ln_exit(self)

    # === Python Interfaces ===

    def __rshift__(self, LogicNode other):
        self.c_append(other, AUTO_CONDITION)
        return other  # Allow chaining

    def __call__(self, object default=None):
        if default is None:
            default = NoAction(auto_connect=False, autogen=True)

        cdef bint inspection_mode = LGM.inspection_mode
        if inspection_mode:
            LOGGER.info('LGM inspection mode temporally disabled to evaluate correctly.')
            LGM.inspection_mode = False

        cdef list path = []
        cdef object value
        value, path = self.c_eval_recursively(path, default)
        LGM.inspection_mode = inspection_mode
        return value

    def __repr__(self):
        return f'<{self.__class__.__name__}>({self.repr!r})'

    def __hash__(self):
        return <uintptr_t> <PyObject*> self

    def append(self, LogicNode child, NodeEdgeCondition condition=AUTO_CONDITION):
        self.c_append(child, condition)

    def overwrite(self, LogicNode new_node, NodeEdgeCondition condition):
        self.c_overwrite(new_node, condition)

    def replace(self, LogicNode original_node, LogicNode new_node):
        self.c_replace(original_node, new_node)

    def eval_recursively(self, list path=None, object default=NO_DEFAULT):
        return self.c_eval_recursively(path, default)

    def list_labels(self) -> dict[str, list[LogicNode]]:
        labels = {}

        def traverse(node):
            for group in node.labels:
                if group not in labels:
                    labels[group] = []
                labels[group].append(node)
            for _, child in node.children.items():
                traverse(child)

        traverse(self)
        return labels

    property leaves:
        def __get__(self):
            cdef LogicNodeFrame* frame = self.subordinates.top
            if not frame:
                yield self
            cdef LogicNode child
            while frame:
                child = <LogicNode> <object> frame.logic_node
                yield from child.leaves
                frame = frame.prev

    property is_leaf:
        def __get__(self):
            return not self.children

    property child_stack:
        def __get__(self):
            cdef LogicNodeFrame* frame = self.subordinates.top
            cdef LogicNode child
            while frame:
                child = <LogicNode> <object> frame.logic_node
                yield child
                frame = frame.prev

    property descendants:
        def __get__(self):
            cdef LogicNodeFrame* frame = self.subordinates.top
            cdef LogicNode child
            while frame:
                child = <LogicNode> <object> frame.logic_node
                yield child
                yield from child.descendants
                frame = frame.prev


cdef class BreakpointNode(LogicNode):
    def __cinit__(self, *, LogicGroup break_from=None, object expression=None, str repr=None, **kwargs):
        self.break_from = break_from
        self.expression = NoAction(auto_connect=False, autogen=True) if expression is None else expression
        self.repr = f'Breakpoint(from={break_from.name})' if repr is None else repr
        self.autogen = True
        self.await_connection = False

    cdef void c_connect(self, LogicNode child):
        if self.subordinates.size:
            raise TooManyChildren(f'{self.__class__.__name__} must not have more than one child node.')

        # Breakpoint will only maintain a virtual connection to child node.
        # That is if a child already have a parent, it will not modify the child's parent pointer.
        # Otherwise, it will assign itself as the parent. (Which will be override if the child append to other node later.)
        cdef LogicNode existing_parent = child.parent

        self.c_append(child, NO_CONDITION)
        if existing_parent is not None:
            child.parent = existing_parent

        self.await_connection = False

    cdef void c_on_enter(self):
        if self.subordinates.size:
            raise TooManyChildren(f'{self.__class__.__name__} must not have more than one child node.')
        # On enter, the breakpoint is disabled and removed from LGM._breakpoint_nodes
        # So that it is not managed and auto connected by LGM anymore.
        self.await_connection = False
        try:
            LogicGroupManager.c_ln_stack_remove(LGM._breakpoint_nodes, self)
        except NodeNotFountError as _:
            pass
        self.c_append(PlaceholderNode(auto_connect=False), NO_CONDITION)
        LogicGroupManager.c_ln_stack_push(LGM._active_nodes, self)
        # LGM.c_ln_enter(self)

    cdef void c_on_exit(self):
        LGM.c_ln_exit(self)

    cdef object c_eval(self, bint enforce_dtype):
        if not self.subordinates.size:
            if LGM.vigilant_mode:
                raise NodeValueError(f'{self} not connected.')
            return self.expression

        cdef LogicNode linked_to = <LogicNode> <object> self.subordinates.top.logic_node
        return linked_to.c_eval(enforce_dtype)

    cdef tuple c_eval_recursively(self, list path=None, object default=NO_DEFAULT):
        if path is None:
            path = [self]
        else:
            path.append(self)

        if not self.subordinates.size:
            if LGM.vigilant_mode:
                raise NodeValueError(f'{self} not connected.')
            return self.expression, path
        cdef LogicNode linked_to = <LogicNode> <object> self.subordinates.top.logic_node
        return linked_to.c_eval_recursively(path, default)

    def __repr__(self):
        if self.subordinates.size:
            return f'<{self.__class__.__name__} connected>(break_from={self.break_from})'
        elif self.await_connection:
            return f'<{self.__class__.__name__} active>(break_from={self.break_from})'
        elif self.parent:
            return f'<{self.__class__.__name__} idle>(break_from={self.break_from})'
        else:
            return f'<{self.__class__.__name__} dangling>(break_from={self.break_from})'

    @classmethod
    def break_(cls, LogicGroup break_from, **kwargs):
        cdef BreakpointNode breakpoint_node = BreakpointNode(break_from=break_from, **kwargs)
        cdef LogicNodeFrame* active_frame = LGM._active_nodes.top
        cdef LogicNode active_node = <LogicNode> <object> active_frame.logic_node
        cdef PlaceholderNode placeholder = active_node.c_get_placeholder()
        active_node.c_replace(placeholder, breakpoint_node)
        return breakpoint_node

    def connect(self, LogicNode child):
        self.c_connect(child)

    property linked_to:
        def __get__(self):
            if self.subordinates.size:
                return <LogicNode> <object> self.subordinates.top.logic_node
            return None


cdef class ActionNode(LogicNode):
    def __cinit__(self, *, object action=None, bint auto_connect=True, **kwargs):
        self.action = action

        if auto_connect:
            self.c_auto_connect()

    cdef void c_auto_connect(self):
        cdef LogicNodeFrame* frame = LGM._active_nodes.top

        # This might come from a forward declaration used by python interface.
        # Graceful exit if not vigilant
        if not frame:
            if LGM.vigilant_mode:
                raise NodeValueError(f'Can not set ActionNode {self} as root node.')
            return

        cdef LogicNode active_node = <LogicNode> <object> frame.logic_node
        cdef PlaceholderNode placeholder = active_node.c_get_placeholder()
        active_node.c_replace(placeholder, self)

    cdef void c_post_eval(self):
        if self.action is not None:
            self.action()

    cdef void c_append(self, LogicNode child, NodeEdgeCondition condition):
        raise TooManyChildren('Action node must not have any child node.')

    cdef void c_on_enter(self):
        raise NodeContextError('ActionNode does not support context management with <with> statement.')

    cdef void c_on_exit(self):
        pass

    cdef tuple c_eval_recursively(self, list path=None, object default=NO_DEFAULT):
        if path is None:
            path = []
        path.append(self)

        value = self.c_eval(False)

        self.c_post_eval()

        if not self.is_leaf:
            raise TooManyChildren('Action node must not have any child node.')

        return value, path


cdef class PlaceholderNode(ActionNode):
    def __cinit__(self, *, **kwargs):
        self.autogen = True
        self.action = NoAction(auto_connect=False, autogen=True)

    cdef object c_eval(self, bint enforce_dtype):
        if LGM.vigilant_mode:
            return self
        return self.action


cdef class NoAction(ActionNode):
    def __cinit__(self, *, ssize_t sig=0, str repr=None, bint auto_connect=True, bint autogen=False, **kwargs):
        self.sig = sig
        self.repr = 'NoAction' if repr is None else repr
        self.autogen = autogen

    cdef object c_eval(self, bint enforce_dtype):
        return self

    def __repr__(self):
        return f'<{self.__class__.__name__}>(sig={self.sig})'

    def __int__(self):
        return self.sig


cdef class LongAction(ActionNode):
    def __cinit__(self, *, ssize_t sig=1, str repr=None, bint auto_connect=True, **kwargs):
        self.sig = sig
        self.repr = 'LongAction' if repr is None else repr

    cdef object c_eval(self, bint enforce_dtype):
        return self

    def __repr__(self):
        return f'<{self.__class__.__name__}>(sig={self.sig})'

    def __int__(self):
        return self.sig


cdef class ShortAction(ActionNode):
    def __cinit__(self, *, ssize_t sig=-1, str repr=None, bint auto_connect=True, **kwargs):
        self.sig = sig
        self.repr = 'ShortAction' if repr is None else repr

    cdef object c_eval(self, bint enforce_dtype):
        return self

    def __repr__(self):
        return f'<{self.__class__.__name__}>(sig={self.sig})'

    def __int__(self):
        return self.sig


cdef class CancelAction(ActionNode):
    def __cinit__(self, *, ssize_t sig=0, str repr=None, bint auto_connect=True, **kwargs):
        self.sig = sig
        self.repr = 'CancelAction' if repr is None else repr

    cdef object c_eval(self, bint enforce_dtype):
        return self

    def __repr__(self):
        return f'<{self.__class__.__name__}>(sig={self.sig})'

    def __int__(self):
        return self.sig


cdef class ClearAction(ActionNode):
    def __cinit__(self, *, ssize_t sig=0, str repr=None, bint auto_connect=True, **kwargs):
        self.sig = sig
        self.repr = 'ClearAction' if repr is None else repr

    cdef object c_eval(self, bint enforce_dtype):
        return self

    def __repr__(self):
        return f'<{self.__class__.__name__}>(sig={self.sig})'

    def __int__(self):
        return self.sig
