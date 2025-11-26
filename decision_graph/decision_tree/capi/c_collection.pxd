from .c_abc cimport LogicGroup


cdef class LogicMapping(LogicGroup):
    cdef dict __dict__
    cdef readonly dict data

    cdef object c_get(self, str key)


cdef class LogicSequence(LogicGroup):
    cdef dict __dict__
    cdef readonly list data

    cdef object c_get(self, ssize_t index)


cdef class LogicGenerator(LogicGroup):
    cdef dict __dict__
    cdef readonly object data

    cdef object c_next(self)
