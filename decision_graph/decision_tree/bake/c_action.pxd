from libcpp cimport bool as c_bool

from cbase.allocator_protocol cimport allocator_protocol

from .c_node cimport LogicNode, dcg_node, dcg_node_type


cdef extern from "decision_graph/decision_tree/bake/c_action.h":
    ctypedef struct dcg_action_node:
        dcg_node base
        c_bool auto_connect
        ssize_t sig
        void* action_data

    dcg_action_node* c_dcg_node_new_action(dcg_node_type action_type, const char* repr, ssize_t sig, void* action_data, allocator_protocol* allocator) noexcept nogil
    void c_dcg_node_free_action(dcg_action_node* node) noexcept nogil

    dcg_action_node* c_dcg_node_new_action_trade(dcg_node_type action_type, allocator_protocol* allocator) noexcept nogil
    dcg_action_node* c_dcg_node_new_action_clear(allocator_protocol* allocator) noexcept nogil

    int c_dcg_node_auto_fill(dcg_node* node) noexcept nogil


cdef extern from "decision_graph/decision_tree/bake/c_logic_group.h":
    ctypedef struct dcg_logic_group_manager:
        pass

    dcg_action_node* c_dcg_node_connect_new_action(dcg_node_type action_type, const char* repr, c_bool auto_connect, ssize_t sig, void* action_data, dcg_logic_group_manager* mgr, allocator_protocol* allocator) noexcept nogil
    dcg_action_node* c_dcg_node_connect_new_action_trade(dcg_node_type action_type, c_bool auto_connect, dcg_logic_group_manager* mgr, allocator_protocol* allocator) noexcept nogil
    dcg_action_node* c_dcg_node_connect_new_action_clear(c_bool auto_connect, dcg_logic_group_manager* mgr, allocator_protocol* allocator) noexcept nogil


cdef class ActionNode(LogicNode):
    pass


cdef class NoAction(ActionNode):
    pass


cdef class LongAction(ActionNode):
    pass


cdef class ShortAction(ActionNode):
    pass


cdef class CancelAction(ActionNode):
    pass


cdef class ClearAction(ActionNode):
    pass
