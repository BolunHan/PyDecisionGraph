from libcpp cimport bool as c_bool

from cbase.allocator_protocol.c_allocator_protocol cimport allocator_protocol

from .c_node cimport dcg_node, dcg_node_type


cdef extern from "decision_graph/decision_tree/bake/c_action.h":
    const char* DCG_DEF_REPR_PLACEHOLDER

    ctypedef struct dcg_action_node:
        dcg_node base
        c_bool auto_connect
        ssize_t sig
        void* action_data

    dcg_action_node* c_dcg_node_new_action(dcg_node_type action_type, const char* repr, c_bool auto_connect, ssize_t sig, void* action_data, allocator_protocol* allocator) noexcept nogil
    void c_dcg_node_free_action(dcg_action_node* node) noexcept nogil
    dcg_action_node* c_dcg_node_new_action_trade(dcg_node_type action_type, c_bool auto_connect, allocator_protocol* allocator) noexcept nogil
    dcg_action_node* c_dcg_node_new_action_clear(c_bool auto_connect, allocator_protocol* allocator) noexcept nogil
    dcg_action_node* c_dcg_node_new_action_placeholder(c_bool auto_connect, allocator_protocol* allocator) noexcept nogil
    dcg_node* c_dcg_node_get_placeholder(dcg_node* node) noexcept nogil
    int c_dcg_node_auto_fill(dcg_node* node) noexcept nogil
