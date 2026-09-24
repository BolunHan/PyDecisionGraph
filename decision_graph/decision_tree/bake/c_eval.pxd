from cbase.allocator_protocol cimport allocator_protocol
from libc.stdint cimport uint64_t
from libcpp cimport bool as c_bool

from .c_hierarchy cimport dcg_node_eval_path, dcg_root_node
from .c_node cimport dcg_node
from .c_var cimport dcg_ret_code, dcg_var_t


cdef extern from "decision_graph/decision_tree/bake/c_eval.h":
    const size_t DCG_EVAL_PATH_INITIAL_CAPACITY

    ctypedef struct dcg_eval_run:
        uint64_t     seq_id
        dcg_ret_code code
        dcg_node*    failed
        dcg_node*    leaf
        size_t       visited
        size_t       depth
        c_bool       inplace

    dcg_node_eval_path* c_dcg_node_eval_path_new(size_t capacity, allocator_protocol* allocator) noexcept nogil
    void c_dcg_node_eval_path_free(dcg_node_eval_path* path) noexcept nogil
    void c_dcg_eval_path_reset(dcg_node_eval_path* path) noexcept nogil
    int c_dcg_eval_path_append(dcg_node_eval_path* path, dcg_node* node) noexcept nogil
    int c_dcg_root_node_eval_path_reserve(dcg_root_node* root, size_t capacity) noexcept nogil
    int c_dcg_root_node_eval_path_append(dcg_root_node* root, dcg_node* node) noexcept nogil

    int c_dcg_node_eval_default(dcg_node* node) noexcept nogil
    int c_dcg_node_eval_hooks(dcg_node* node) noexcept nogil
    int c_dcg_node_eval(dcg_node* node) noexcept nogil
    int c_dcg_node_dryrun(dcg_node* node, dcg_var_t* out) noexcept nogil

    uint64_t c_dcg_eval_gen_seq_id(const dcg_node* node) noexcept nogil
    dcg_node* c_dcg_eval_select_child(const dcg_node* node) noexcept nogil
    int c_dcg_node_eval_visit(dcg_node* node, dcg_eval_run* run, size_t depth, dcg_root_node* root, dcg_node_eval_path* path) noexcept nogil
    void c_dcg_eval_path_outcome(dcg_root_node* root, dcg_node_eval_path* path, const dcg_eval_run* run, dcg_node* leaf, dcg_node* failed) noexcept nogil
    int c_dcg_eval_walk(dcg_node* node, dcg_root_node* root, dcg_node_eval_path* path) noexcept nogil
    int c_dcg_node_eval_graph(dcg_node* node, dcg_node_eval_path* path) noexcept nogil
    int c_dcg_root_node_eval(dcg_root_node* root) noexcept nogil
