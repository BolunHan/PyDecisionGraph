from cpython.unicode cimport PyUnicode_AsUTF8, PyUnicode_FromString
from libc.stdint cimport uintptr_t

from .c_allocator_protocol cimport DCG_DEFAULT_ALLOCATOR
from .c_var cimport c_dcg_var_pypack


cdef class NodeEdgeCondition:
    def __init__(self, object py_value=None, str repr=None):
        if py_value is None:
            raise ValueError('A user condition needs a value; the five built-ins are module constants.')

        cdef dcg_var_t val
        c_dcg_var_pypack(&val, py_value)

        self.header = c_dcg_edge_new(val, PyUnicode_AsUTF8(repr) if repr is not None else NULL, DCG_DEFAULT_ALLOCATOR)
        if not self.header:
            raise MemoryError('Failed to allocate an edge condition.')

        self.owner = True

    def __dealloc__(self):
        if not self.owner:
            return

        if self.header:
            c_dcg_condition_free(self.header)

    @staticmethod
    cdef NodeEdgeCondition c_from_header(dcg_node_edge_condition* header, bint owner=False):
        cdef NodeEdgeCondition instance = NodeEdgeCondition.__new__(NodeEdgeCondition)
        instance.header = header
        instance.owner = owner
        return instance

    # === Python Dunders ===

    def __hash__(self):
        if self.header == NULL:
            return 0
        if c_dcg_condition_is_sentinel(self.header):
            return <uintptr_t> self.header.type
        return <uintptr_t> self.header

    def __eq__(self, NodeEdgeCondition other):
        if not isinstance(other, NodeEdgeCondition):
            return NotImplemented
        return c_dcg_condition_equals(self.header, other.header)

    def __ne__(self, NodeEdgeCondition other):
        if not isinstance(other, NodeEdgeCondition):
            return NotImplemented
        return not c_dcg_condition_equals(self.header, other.header)

    def __repr__(self):
        if not self.header:
            return f'<{self.__class__.__name__} (Uninitialized)>'
        cdef char buf[DCG_NODE_STRING_MAXLEN]
        c_dcg_condition_format(self.header, NULL, buf, sizeof(buf))
        return f'<{self.__class__.__name__}>({PyUnicode_FromString(buf)})'

    def __str__(self):
        if not self.header:
            return f'<{self.__class__.__name__} (Uninitialized)>'
        cdef char buf[DCG_NODE_STRING_MAXLEN]
        c_dcg_condition_format(self.header, NULL, buf, sizeof(buf))
        return PyUnicode_FromString(buf)

    property is_none:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return c_dcg_condition_is_none(self.header)

    property is_else:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return c_dcg_condition_is_else(self.header)

    property is_auto:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return c_dcg_condition_is_auto(self.header)

    property is_binary:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return c_dcg_condition_is_binary(self.header)


cdef class ConditionAny(NodeEdgeCondition):
    @staticmethod
    cdef ConditionAny c_from_header(dcg_node_edge_condition* header, bint owner=False):
        cdef ConditionAny instance = ConditionAny.__new__(ConditionAny)
        instance.header = header
        instance.owner = owner
        return instance


cdef class ConditionElse(NodeEdgeCondition):
    @staticmethod
    cdef ConditionElse c_from_header(dcg_node_edge_condition* header, bint owner=False):
        cdef ConditionElse instance = ConditionElse.__new__(ConditionElse)
        instance.header = header
        instance.owner = owner
        return instance


cdef class ConditionAuto(NodeEdgeCondition):
    @staticmethod
    cdef ConditionAuto c_from_header(dcg_node_edge_condition* header, bint owner=False):
        cdef ConditionAuto instance = ConditionAuto.__new__(ConditionAuto)
        instance.header = header
        instance.owner = owner
        return instance


cdef class BinaryCondition(NodeEdgeCondition):
    @staticmethod
    cdef BinaryCondition c_from_header(dcg_node_edge_condition* header, bint owner=False):
        cdef BinaryCondition instance = BinaryCondition.__new__(BinaryCondition)
        instance.header = header
        instance.owner = owner
        return instance


cdef class ConditionTrue(BinaryCondition):
    @staticmethod
    cdef ConditionTrue c_from_header(dcg_node_edge_condition* header, bint owner=False):
        cdef ConditionTrue instance = ConditionTrue.__new__(ConditionTrue)
        instance.header = header
        instance.owner = owner
        return instance

    def __bool__(self):
        return True

    def __int__(self):
        return 1


cdef class ConditionFalse(BinaryCondition):
    @staticmethod
    cdef ConditionFalse c_from_header(dcg_node_edge_condition* header, bint owner=False):
        cdef ConditionFalse instance = ConditionFalse.__new__(ConditionFalse)
        instance.header = header
        instance.owner = owner
        return instance

    def __bool__(self):
        return False

    def __int__(self):
        return 0


c_dcg_condition_init_globals()

cdef dcg_node_edge_condition* C_NO_CONDITION    = &__DCG_NO_CONDITION
cdef dcg_node_edge_condition* C_ELSE_CONDITION  = &__DCG_ELSE_CONDITION
cdef dcg_node_edge_condition* C_AUTO_CONDITION  = &__DCG_AUTO_CONDITION
cdef dcg_node_edge_condition* C_TRUE_CONDITION  = &__DCG_TRUE_CONDITION
cdef dcg_node_edge_condition* C_FALSE_CONDITION = &__DCG_FALSE_CONDITION

cdef ConditionAny NO_CONDITION                  = ConditionAny.c_from_header(C_NO_CONDITION, False)
cdef ConditionElse ELSE_CONDITION               = ConditionElse.c_from_header(C_ELSE_CONDITION, False)
cdef ConditionAuto AUTO_CONDITION               = ConditionAuto.c_from_header(C_AUTO_CONDITION, False)
cdef ConditionTrue TRUE_CONDITION               = ConditionTrue.c_from_header(C_TRUE_CONDITION, False)
cdef ConditionFalse FALSE_CONDITION             = ConditionFalse.c_from_header(C_FALSE_CONDITION, False)

globals()['NO_CONDITION']                       = NO_CONDITION
globals()['ELSE_CONDITION']                     = ELSE_CONDITION
globals()['AUTO_CONDITION']                     = AUTO_CONDITION
globals()['TRUE_CONDITION']                     = TRUE_CONDITION
globals()['FALSE_CONDITION']                    = FALSE_CONDITION
