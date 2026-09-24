from libc.stdint cimport uint64_t

from cbase.allocator_protocol cimport allocator_protocol

from .c_hierarchy cimport RootLogicNode, dcg_root_node
from .c_node cimport dcg_node
from .c_var cimport dcg_ret_code


cdef extern from "decision_graph/decision_tree/bake/c_bake.h":
    ctypedef enum dcg_bake_flag:
        DCG_BAKE_FLAG_NONE
        DCG_BAKE_FLAG_VALIDATE_ONLY

    ctypedef struct dcg_bake_input:
        uint64_t flags

    ctypedef struct dcg_bake_report:
        dcg_ret_code code
        dcg_node* node
        size_t errors
        size_t nodes
        size_t depth
        size_t locked
        size_t sealed
        size_t capacity

    void c_dcg_bake_input_init(dcg_bake_input* input) noexcept nogil
    dcg_bake_report* c_dcg_bake_report_new(allocator_protocol* allocator) noexcept nogil
    void c_dcg_bake_report_free(dcg_bake_report* report) noexcept nogil
    int c_dcg_root_node_bake(dcg_root_node* root, const dcg_bake_input* input, dcg_bake_report* report) noexcept nogil


cdef class BakeReport:
    cdef dcg_bake_report* header
    cdef bint owner

    @staticmethod
    cdef BakeReport c_from_header(dcg_bake_report* header, bint owner=?)


cpdef BakeReport c_dcg_bake_root(RootLogicNode root, bint validate_only=?)
