from cbase.allocator_protocol.c_allocator_protocol cimport allocator_protocol
from cbase.bytemap.c_bytemap cimport bytemap
from libcpp cimport bool as c_bool

from .c_const cimport VariableNode, dcg_variable_node
from .c_logic_group cimport LogicGroup, dcg_logic_group
from .c_node cimport dcg_node
from .c_var cimport dcg_var_t, dcg_var_type


cdef extern from "decision_graph/decision_tree/bake/c_collections.h":
    const size_t DCG_MAPPING_DEFAULT_CAPACITY

    ctypedef struct dcg_mapping_lgroup:
        dcg_logic_group base
        bytemap idx_mapping
        dcg_var_t* slots
        size_t n_slots
        size_t capacity
        c_bool frozen

    dcg_mapping_lgroup* c_dcg_mapping_lgroup_new(const char* name, size_t capacity, allocator_protocol* allocator) noexcept nogil
    void c_dcg_mapping_lgroup_dealloc(dcg_mapping_lgroup* lgroup) noexcept nogil
    void c_dcg_mapping_lgroup_free(dcg_mapping_lgroup* lgroup) noexcept nogil

    int c_dcg_mapping_lgroup_set(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, dcg_var_t* value) noexcept nogil
    int c_dcg_mapping_lgroup_set_ref(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, dcg_var_t* value) noexcept nogil
    int c_dcg_mapping_lgroup_set_ptr(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, void* value) noexcept nogil
    int c_dcg_mapping_lgroup_set_double(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, double value) noexcept nogil
    int c_dcg_mapping_lgroup_set_int(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, ssize_t value) noexcept nogil
    int c_dcg_mapping_lgroup_set_offset(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, ssize_t value) noexcept nogil
    int c_dcg_mapping_lgroup_set_bool(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, c_bool value) noexcept nogil

    dcg_variable_node* c_dcg_mapping_lgroup_get_node(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, allocator_protocol* allocator) noexcept nogil

    int c_dcg_node_mapping_var_node_eval_hook(dcg_node* header, void* user_data) noexcept nogil

    dcg_var_t* c_dcg_mapping_lgroup_get_slot(const dcg_mapping_lgroup* lgroup, const char* key, size_t key_len) noexcept nogil
    int c_dcg_mapping_lgroup_get_create_slot(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, const dcg_var_type* var_type, dcg_var_t** out) noexcept nogil
    int c_dcg_mapping_lgroup_get_var_idx(const dcg_mapping_lgroup* lgroup, const char* key, size_t key_len, size_t* out_index) noexcept nogil


cdef class LogicMapping(LogicGroup):
    cdef dcg_var_t* c_get_slot(self, const char* key, size_t key_len)

    cdef dcg_var_t* c_get_create_slot(self, const char* key, size_t key_len, const dcg_var_type* var_type) except NULL


cdef class AttrExpression(VariableNode):
    pass
