from libc.stdint cimport uint64_t
from libcpp cimport bool as c_bool

from cbase.allocator_protocol cimport allocator_protocol

from .c_node cimport LogicNode, dcg_node
from .c_var cimport dcg_ret_code, dcg_var_t


cdef extern from "decision_graph/decision_tree/bake/c_logic_group.h":
    ctypedef struct dcg_logic_group:
        pass


cdef extern from "decision_graph/decision_tree/bake/c_hierarchy.h":
    ctypedef struct dcg_node_eval_path:
        dcg_node** node
        dcg_var_t* eval_val
        size_t capacity
        size_t  n_nodes
        dcg_ret_code code
        dcg_node* leaf
        dcg_node* failed
        uint64_t seq_id

    ctypedef struct dcg_root_node:
        dcg_node base
        dcg_node_eval_path eval_path
        c_bool inherit_contexts

    ctypedef struct dcg_breakpoint_node:
        dcg_node base
        dcg_logic_group* break_from
        c_bool await_connection

    dcg_root_node* c_dcg_node_new_root(const char* repr, allocator_protocol* allocator) noexcept nogil
    dcg_breakpoint_node* c_dcg_node_new_breakpoint(dcg_logic_group* break_from, const char* repr, allocator_protocol* allocator) noexcept nogil
    void c_dcg_node_free_root(dcg_root_node* node) noexcept nogil
    void c_dcg_node_free_breakpoint(dcg_breakpoint_node* node) noexcept nogil

    void c_dcg_node_free_generic(dcg_node* node) noexcept nogil
    int c_dcg_node_teardown_root(dcg_node* node) noexcept nogil
    int c_dcg_node_remove(dcg_node* node) noexcept nogil
    int c_dcg_node_clear_children(dcg_node* node) noexcept nogil
    int c_dcg_node_clean(dcg_node* node) noexcept nogil


cdef extern from "decision_graph/decision_tree/bake/c_eval.h":
    int c_dcg_root_node_eval(dcg_root_node* root) noexcept nogil


cdef class NodeEvalPathView:
    cdef const dcg_node_eval_path* header

    @staticmethod
    cdef inline NodeEvalPathView c_from_header(const dcg_node_eval_path* path)


cdef class RootLogicNode(LogicNode):
    cdef readonly str name
    cdef readonly NodeEvalPathView eval_path

    cpdef object bake(self, bint validate_only=?)


cdef class BreakpointNode(LogicNode):
    cdef readonly object break_from
