from libcpp cimport bool as c_bool

from cbase.allocator_protocol.c_allocator_protocol cimport allocator_protocol
from cbase.bytemap.c_bytemap cimport bytemap

from .c_const cimport dcg_variable_node
from .c_logic_group cimport dcg_logic_group
from .c_var cimport dcg_var_t


cdef extern from "decision_graph/decision_tree/bake/c_collection.h":
    ctypedef struct dcg_mapping_lgroup:
        dcg_logic_group base
        bytemap idx_mapping
        dcg_var_t* slots
        size_t n_slots
        size_t capacity

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

    dcg_var_t* c_dcg_mapping_lgroup_get_var(const dcg_mapping_lgroup* lgroup, const char* key, size_t key_len) noexcept nogil
    dcg_variable_node* c_dcg_mapping_lgroup_get_node(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len) noexcept nogil

    dcg_var_t* c_dcg_mapping_lgroup_get_slot(const dcg_mapping_lgroup* lgroup, const char* key, size_t key_len) noexcept nogil
    dcg_var_t* c_dcg_mapping_lgroup_get_create_slot(dcg_mapping_lgroup* lgroup, const char* key, size_t key_len) noexcept nogil
