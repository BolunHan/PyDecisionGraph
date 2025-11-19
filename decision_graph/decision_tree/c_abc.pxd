from cpython.pystate cimport PyThreadState

cdef extern from "Python.h":
    void PyThreadState_EnterTracing(PyThreadState *tstate)
    void PyThreadState_LeaveTracing(PyThreadState *tstate)


cdef class SkipContextsBlock:
    cdef type skip_exception
    cdef bint tracer_override
    cdef public bint default_entry_check
    cdef object cframe
    cdef tuple enter_line

    cdef size_t cframe_tracer_sig_count
    cdef object cframe_tracer

    cdef size_t global_tracer_sig_count
    cdef object global_tracer

    cdef size_t global_profiler_sig_count
    cdef object global_profiler

    cdef bint c_entry_check(self)

    cdef void c_on_enter(self)

    cdef void c_on_exit(self)
