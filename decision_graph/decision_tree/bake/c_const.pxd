from libcpp cimport bool as c_bool

from cbase.allocator_protocol.c_allocator_protocol cimport allocator_protocol

from .c_node cimport dcg_node, dcg_node_type
from .c_var cimport dcg_var_t


cdef extern from "decision_graph/decision_tree/bake/c_node.h":
    ctypedef struct dcg_logic_group:
        pass


cdef extern from "decision_graph/decision_tree/bake/c_const.h":
    ctypedef struct dcg_constant_node:
        dcg_node base

    ctypedef struct dcg_variable_node:
        dcg_node base
        const char* key
        dcg_logic_group* logic_group

    dcg_constant_node* c_dcg_node_new_const(dcg_node_type ntype, const char* repr, allocator_protocol* allocator) noexcept nogil
    void c_dcg_node_free_const(dcg_constant_node* node) noexcept nogil

    dcg_constant_node* c_dcg_node_new_const_value(const char* repr, dcg_var_t value, allocator_protocol* allocator) noexcept nogil
    dcg_constant_node* c_dcg_node_new_const_bool(c_bool value, allocator_protocol* allocator) noexcept nogil
    dcg_constant_node* c_dcg_node_new_const_double(double value, allocator_protocol* allocator) noexcept nogil
    dcg_constant_node* c_dcg_node_new_const_int(ssize_t value, allocator_protocol* allocator) noexcept nogil
    dcg_constant_node* c_dcg_node_new_const_string(const char* value, allocator_protocol* allocator) noexcept nogil
    const dcg_var_t* c_dcg_node_const_get(const dcg_constant_node* node) noexcept nogil
    int c_dcg_node_const_set(dcg_constant_node* node, dcg_var_t value) noexcept nogil

    dcg_variable_node* c_dcg_node_new_var(const char* repr, const char* key, size_t key_len, dcg_var_t* value, dcg_logic_group* group, allocator_protocol* allocator) noexcept nogil
    void c_dcg_node_free_var(dcg_variable_node* node) noexcept nogil
