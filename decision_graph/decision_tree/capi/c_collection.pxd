from .c_abc cimport LogicGroup


cdef class LogicMapping(LogicGroup):
    cdef readonly dict data

    cdef object c_get(self, str key)


cdef class LogicSequence(LogicGroup):
    cdef readonly list data

    cdef object c_get(self, size_t index)


cdef class LogicGenerator(LogicGroup):
    cdef readonly object data

    cdef object c_next(self)
