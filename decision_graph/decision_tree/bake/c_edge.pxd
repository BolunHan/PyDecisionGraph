from libc.stdio cimport FILE
from libcpp cimport bool as c_bool

from cbase.allocator_protocol.c_allocator_protocol cimport allocator_protocol
from cbase.bytemap cimport BoundByteMap, bytemap

from .c_var cimport dcg_var_t


# This can not be cimport normally, without triggering a cyclic import error.
cdef extern from "decision_graph/decision_tree/bake/c_node.h":
    const int DCG_NODE_STRING_MAXLEN


cdef extern from "decision_graph/decision_tree/bake/c_edge.h":
    const int DCG_EDGE_REPR_MAXLEN

    ctypedef enum dcg_node_edge_type:
        DCG_NODE_EDGE_VALUE
        DCG_NODE_EDGE_NONE
        DCG_NODE_EDGE_ELSE
        DCG_NODE_EDGE_AUTO
        DCG_NODE_EDGE_TRUE
        DCG_NODE_EDGE_FALSE

    ctypedef struct dcg_node_edge_condition:
        dcg_node_edge_type type
        dcg_var_t value
        char repr[DCG_EDGE_REPR_MAXLEN]

    c_bool __DCG_CONDITION_INITIALIZED
    dcg_node_edge_condition __DCG_NO_CONDITION
    dcg_node_edge_condition __DCG_ELSE_CONDITION
    dcg_node_edge_condition __DCG_AUTO_CONDITION
    dcg_node_edge_condition __DCG_TRUE_CONDITION
    dcg_node_edge_condition __DCG_FALSE_CONDITION

    void c_dcg_condition_init_globals() noexcept nogil
    c_bool c_dcg_condition_is_sentinel(const dcg_node_edge_condition* cond) noexcept nogil
    c_bool c_dcg_condition_is_none(const dcg_node_edge_condition* cond) noexcept nogil
    c_bool c_dcg_condition_is_else(const dcg_node_edge_condition* cond) noexcept nogil
    c_bool c_dcg_condition_is_auto(const dcg_node_edge_condition* cond) noexcept nogil
    c_bool c_dcg_condition_is_true(const dcg_node_edge_condition* cond) noexcept nogil
    c_bool c_dcg_condition_is_false(const dcg_node_edge_condition* cond) noexcept nogil
    c_bool c_dcg_condition_is_binary(const dcg_node_edge_condition* cond) noexcept nogil

    c_bool c_dcg_condition_equals(const dcg_node_edge_condition* lhs, const dcg_node_edge_condition* rhs) noexcept nogil
    c_bool c_dcg_condition_matches(const dcg_node_edge_condition* cond, const dcg_var_t* value) noexcept nogil

    int c_dcg_condition_init(dcg_node_edge_condition* cond, dcg_var_t value, const char* repr) noexcept nogil
    dcg_node_edge_condition* c_dcg_edge_new(dcg_var_t value, const char* repr, allocator_protocol* allocator) noexcept nogil
    void c_dcg_condition_dealloc(dcg_node_edge_condition* cond) noexcept nogil
    void c_dcg_condition_free(dcg_node_edge_condition* cond) noexcept nogil

    const char* c_dcg_condition_repr(const dcg_node_edge_condition* cond) noexcept nogil
    int c_dcg_condition_format(const dcg_node_edge_condition* cond, const char* fallback_prefix, char* out, size_t cap) noexcept nogil
    int c_dcg_condition_print(const dcg_node_edge_condition* cond, FILE* stream) noexcept nogil


cdef class NodeEdgeCondition:
    cdef dcg_node_edge_condition* header
    cdef bint owner

    @staticmethod
    cdef NodeEdgeCondition c_from_header(dcg_node_edge_condition* header, bint owner=?)


cdef class ConditionAny(NodeEdgeCondition):
    @staticmethod
    cdef ConditionAny c_from_header(dcg_node_edge_condition* header, bint owner=?)


cdef class ConditionElse(NodeEdgeCondition):
    @staticmethod
    cdef ConditionElse c_from_header(dcg_node_edge_condition* header, bint owner=?)


cdef class ConditionAuto(NodeEdgeCondition):
    @staticmethod
    cdef ConditionAuto c_from_header(dcg_node_edge_condition* header, bint owner=?)


cdef class BinaryCondition(NodeEdgeCondition):
    @staticmethod
    cdef BinaryCondition c_from_header(dcg_node_edge_condition* header, bint owner=?)


cdef class ConditionTrue(BinaryCondition):
    @staticmethod
    cdef ConditionTrue c_from_header(dcg_node_edge_condition* header, bint owner=?)


cdef class ConditionFalse(BinaryCondition):
    @staticmethod
    cdef ConditionFalse c_from_header(dcg_node_edge_condition* header, bint owner=?)


cdef class EdgeConditionRegistry(BoundByteMap):
    cdef void* _ws_key_buf

    @staticmethod
    cdef EdgeConditionRegistry c_from_header(bytemap* header, bint owner=?)


cdef dcg_node_edge_condition* C_NO_CONDITION
cdef dcg_node_edge_condition* C_ELSE_CONDITION
cdef dcg_node_edge_condition* C_AUTO_CONDITION
cdef dcg_node_edge_condition* C_TRUE_CONDITION
cdef dcg_node_edge_condition* C_FALSE_CONDITION

cdef NodeEdgeCondition NO_CONDITION
cdef NodeEdgeCondition ELSE_CONDITION
cdef NodeEdgeCondition AUTO_CONDITION
cdef NodeEdgeCondition TRUE_CONDITION
cdef NodeEdgeCondition FALSE_CONDITION

cdef EdgeConditionRegistry EDGE_REGISTRY
