import linecache
import operator
import sys
from typing import final, Any
import warnings

from cpython.exc cimport PyErr_SetString
from cpython.pystate cimport PyThreadState_Get
from cython cimport exceptval

from . import LOGGER
from .exc import TooFewChildren, TooManyChildren, EdgeValueError, NodeValueError, NodeNotFountError

LOGGER = LOGGER.getChild('abc')

cdef dict GLOBAL_SINGLETON = {}


cdef class Singleton:
    def __cinit__(self):
        cdef tuple reg_key = (self.__class__.__module__, self.__class__.__qualname__)
        if reg_key in GLOBAL_SINGLETON:
            raise RuntimeError(f'Can not initialize new singleton {self.__class__}')
        else:
            GLOBAL_SINGLETON[reg_key] = self


cdef class ConditionElse(Singleton):
    def __str__(self):
        return ''

    def __repr__(self):
        return f'<{self.__class__.__name__}>'


ELSE_CONDITION = NO_CONDITION = ConditionElse()


class EmptyBlock(Exception):
    pass


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
    def __cinit__(self, object expression, type dtype=None, str repr=None):
        self.expression = expression
        self.dtype = dtype
        self.repr = repr if repr is not None else str(expression)

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
        return f"LogicExpression(dtype={'Any' if self.dtype is None else self.dtype.__name__}, repr={self.repr})"
