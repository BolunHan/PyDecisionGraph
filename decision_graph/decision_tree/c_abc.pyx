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

        # self.cframe = sys._getframe()
        # self.cframe_tracer_sig_count = 0
        # self.cframe_tracer = self.cframe.f_trace
        # self.cframe.f_trace = self.cframe_tracer_skipper

        # self.global_tracer_sig_count = 0
        # self.global_tracer = sys.gettrace()
        # sys.settrace(self.global_tracer_skipper)

        self.global_profiler_sig_count = 0
        self.global_profiler = sys.getprofile()
        sys.setprofile(self.global_profile_tracer)

        self.tracer_override = True
        PyThreadState_LeaveTracing(tstate)
        return self

    def __exit__(self, exc_type, exc_value, exc_traceback):
        cdef PyThreadState* tstate = PyThreadState_Get()
        PyThreadState_EnterTracing(tstate)

        if self.tracer_override:
            # self.cframe.f_trace = self.cframe_tracer
            # sys.settrace(self.global_tracer)
            sys.setprofile(self.global_profiler)
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

    def cframe_tracer_skipper(self, frame, event, arg):
        print('[cframe_tracer_skipper] skipping...', frame, event, arg)
        self.cframe_tracer_sig_count += 1
        return self.cframe_tracer_skipper

    def global_tracer_skipper(self, frame, event, arg):
        print('[global_tracer_skipper] skipping...', frame, event, arg)
        self.global_tracer_sig_count += 1
        # if event == 'call':
        #     raise self.skip_exception('')
        return self.global_tracer_skipper

    def global_profile_tracer(self, frame, event, arg):
        print('[global_profile_tracer] skipping...', frame, event, arg)
        self.global_profiler_sig_count += 1
        if event == 'c_call':
            raise self.skip_exception('')
        return self.global_profile_tracer
