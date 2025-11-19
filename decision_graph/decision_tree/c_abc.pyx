from typing import final
import sys

from cpython.exc cimport PyErr_SetString
from cpython.pystate cimport PyThreadState_Get
from cython cimport exceptval

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
        self.cframe_sig_count = 0
        self.tracer_sig_count = 0
        self.profile_sig_count = 0

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
        self.cframe_sig_count = 0
        self.tracer_sig_count = 0
        self.profile_sig_count = 0
        self.original_tracer = sys.gettrace()
        self.outer_frame = sys._getframe()
        self.outer_frame.f_trace = self.cframe_tracer
        sys.settrace(self.skip_tracer)
        sys.setprofile(self.profile_tracer)
        self.tracer_override = True

        PyThreadState_LeaveTracing(tstate)
        return self

    def __exit__(self, exc_type, exc_value, exc_traceback):
        cdef PyThreadState* tstate = PyThreadState_Get()
        PyThreadState_EnterTracing(tstate)

        if self.tracer_override:
            self.outer_frame.f_trace = self.original_tracer
            sys.settrace(self.original_tracer)
            sys.setprofile(None)
            self.tracer_override = False
        PyThreadState_LeaveTracing(tstate)

        if exc_type is None:
            self.c_on_exit()
            return None

        if issubclass(exc_type, self.skip_exception):
            # in this case, the block is not even entered, so no need to call c_on_exit cleanup.
            return True

        self.c_on_exit()
        return False

    def profile_tracer(self, frame, event, arg):
        # print('[profile_tracer] skipping...', frame, event, arg)
        self.profile_sig_count += 1
        if event == 'c_call':
            raise self.skip_exception('')
        return self.profile_tracer

    def cframe_tracer(self, frame, event, arg):
        # print('[cframe_tracer] skipping...', frame, event, arg)
        # raise self.skip_exception('')
        self.cframe_sig_count += 1
        # if self.cframe_sig_count + self.tracer_sig_count >= 2:
        #     raise self.skip_exception('')
        return self.cframe_tracer

    def skip_tracer(self, frame, event, arg):
        # print('[skip_tracer] skipping...', frame, event, arg)
        self.tracer_sig_count += 1
        # if self.cframe_sig_count + self.tracer_sig_count > 2:
        #     raise self.skip_exception('')
        return self.skip_tracer
