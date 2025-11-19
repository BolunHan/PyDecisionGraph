from cpython.pystate cimport PyThreadState

cdef extern from "Python.h":
    void PyThreadState_EnterTracing(PyThreadState *tstate)
    void PyThreadState_LeaveTracing(PyThreadState *tstate)


cdef class SkipContextsBlock:
    cdef type skip_exception
    cdef object original_tracer
    cdef bint tracer_override
    cdef object outer_frame
    cdef size_t profile_sig_count
    cdef size_t cframe_sig_count
    cdef size_t tracer_sig_count

    cdef public bint default_entry_check

    cdef bint c_entry_check(self)

    cdef void c_on_enter(self)

    cdef void c_on_exit(self)
