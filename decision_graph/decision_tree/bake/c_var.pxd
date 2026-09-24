from libc.stdint cimport uint64_t
from libc.stdio cimport FILE
from libcpp cimport bool as c_bool

from cbase.allocator_protocol.c_allocator_protocol cimport allocator_protocol


cdef extern from "decision_graph/decision_tree/bake/c_var.h":
    const c_bool DCG_VAR_HAS_SESSION_TIME
    const int DCG_VAR_STRING_MAXLEN
    const c_bool DCG_VIGILANT

    ctypedef enum dcg_ret_code:
        DCG_OK
        DCG_ERR_INVALID_ARG
        DCG_ERR_INVALID_BUF
        DCG_ERR_OOM
        DCG_ERR_NOT_FOUND
        DCG_ERR_FULL
        DCG_ERR_BAD_CAST
        DCG_ERR_FORMAT
        DCG_ERR_TYPE
        DCG_ERR_CYCLE
        DCG_ERR_BUSY
        DCG_ERR_DUPLICATE
        DCG_ERR_EDGE
        DCG_ERR_UNRESOLVED
        DCG_ERR_RANGE
        DCG_ERR_HOOK
        DCG_ERR_NO_MATCH
        DCG_ERR_MATH
        DCG_ERR_UNBOUND

    ctypedef enum dcg_var_numeric:
        VAR_NUMERIC_NONE
        VAR_NUMERIC_INT
        VAR_NUMERIC_DOUBLE

    ctypedef enum dcg_var_type_mask:
        VAR_TYPE_BASE_MASK
        VAR_TYPE_REF_MASK
        VAR_TYPE_REF_SHIFT
        VAR_TYPE_REF_LEVEL1
        VAR_TYPE_REF_LEVEL2

    ctypedef enum dcg_var_type:
        VAR_TYPE_RAW_PTR
        VAR_TYPE_STRING
        VAR_TYPE_BOOL
        VAR_TYPE_DOUBLE
        VAR_TYPE_INT
        VAR_TYPE_OFFSET
        VAR_TYPE_TIME
        VAR_TYPE_DATE
        VAR_TYPE_DATETIME
        VAR_TYPE_D_VECTOR
        VAR_TYPE_D_MATRIX
        VAR_TYPE_RESERVED
        VAR_TYPE_RAW_PTR_REF
        VAR_TYPE_STRING_REF
        VAR_TYPE_BOOL_REF
        VAR_TYPE_DOUBLE_REF
        VAR_TYPE_INT_REF
        VAR_TYPE_OFFSET_REF
        VAR_TYPE_TIME_REF
        VAR_TYPE_DATE_REF
        VAR_TYPE_DATETIME_REF
        VAR_TYPE_D_VECTOR_REF
        VAR_TYPE_D_MATRIX_REF
        VAR_TYPE_INFERRED
        VAR_TYPE_RAW_PTR_REF_REF
        VAR_TYPE_STRING_REF_REF
        VAR_TYPE_BOOL_REF_REF
        VAR_TYPE_DOUBLE_REF_REF
        VAR_TYPE_INT_REF_REF
        VAR_TYPE_OFFSET_REF_REF
        VAR_TYPE_TIME_REF_REF
        VAR_TYPE_DATE_REF_REF
        VAR_TYPE_DATETIME_REF_REF
        VAR_TYPE_D_VECTOR_REF_REF
        VAR_TYPE_D_MATRIX_REF_REF

    ctypedef struct dcg_d_vector_t:
        double* data
        size_t n

    ctypedef struct dcg_d_matrix_t:
        double* data
        size_t n_rows
        size_t n_cols
        c_bool row_major

    ctypedef union dcg_var_variant:
        void* as_ptr
        const char* as_string
        c_bool as_bool
        double as_double
        ssize_t as_int
        ssize_t as_offset
        uint64_t as_bits
        dcg_d_vector_t* as_dvector
        dcg_d_matrix_t* as_dmatrix
        const void* as_ref

    ctypedef struct dcg_var_t:
        dcg_var_type dtype
        dcg_var_variant value

    dcg_d_vector_t* c_dcg_d_vector_new(size_t n, allocator_protocol* allocator) noexcept nogil
    dcg_d_vector_t* c_dcg_d_vector_new_child(size_t n, allocator_protocol* allocator, const void* parent) noexcept nogil
    void c_dcg_d_vector_free(dcg_d_vector_t* vector) noexcept nogil
    double* c_dcg_d_vector_data(const dcg_d_vector_t* vector) noexcept nogil
    size_t c_dcg_d_vector_size(const dcg_d_vector_t* vector) noexcept nogil
    double c_dcg_d_vector_at(const dcg_d_vector_t* vector, size_t index) noexcept nogil
    int c_dcg_d_vector_set(dcg_d_vector_t* vector, size_t index, double value) noexcept nogil

    dcg_d_matrix_t* c_dcg_d_matrix_new(size_t n_rows, size_t n_cols, c_bool row_major, allocator_protocol* allocator) noexcept nogil
    dcg_d_matrix_t* c_dcg_d_matrix_new_child(size_t n_rows, size_t n_cols, c_bool row_major, allocator_protocol* allocator, const void* parent) noexcept nogil
    void c_dcg_d_matrix_free(dcg_d_matrix_t* matrix) noexcept nogil
    double* c_dcg_d_matrix_data(const dcg_d_matrix_t* matrix) noexcept nogil
    size_t c_dcg_d_matrix_rows(const dcg_d_matrix_t* matrix) noexcept nogil
    size_t c_dcg_d_matrix_cols(const dcg_d_matrix_t* matrix) noexcept nogil
    double c_dcg_d_matrix_at(const dcg_d_matrix_t* matrix, size_t row, size_t col) noexcept nogil
    int c_dcg_d_matrix_set(dcg_d_matrix_t* matrix, size_t row, size_t col, double value) noexcept nogil

    dcg_var_t* c_dcg_var_new(allocator_protocol* allocator) noexcept nogil
    dcg_var_t* c_dcg_var_new_bool(c_bool value, allocator_protocol* allocator) noexcept nogil
    dcg_var_t* c_dcg_var_new_double(double value, allocator_protocol* allocator) noexcept nogil
    dcg_var_t* c_dcg_var_new_int(ssize_t value, allocator_protocol* allocator) noexcept nogil
    dcg_var_t* c_dcg_var_new_offset(ssize_t value, allocator_protocol* allocator) noexcept nogil
    dcg_var_t* c_dcg_var_new_string(const char* value, allocator_protocol* allocator) noexcept nogil
    dcg_var_t* c_dcg_var_new_ptr(void* value, allocator_protocol* allocator) noexcept nogil
    dcg_var_t* c_dcg_var_new_dvector(size_t n, allocator_protocol* allocator) noexcept nogil
    dcg_var_t* c_dcg_var_new_dmatrix(size_t n_rows, size_t n_cols, c_bool row_major, allocator_protocol* allocator) noexcept nogil
    dcg_var_t* c_dcg_var_new_ref(const dcg_var_t* src, allocator_protocol* allocator) noexcept nogil
    void c_dcg_var_free(dcg_var_t* var) noexcept nogil

    int c_dcg_var_init(dcg_var_t* var) noexcept nogil
    int c_dcg_var_init_reserved(dcg_var_t* var) noexcept nogil
    int c_dcg_var_init_bool(dcg_var_t* var, c_bool value) noexcept nogil
    int c_dcg_var_init_double(dcg_var_t* var, double value) noexcept nogil
    int c_dcg_var_init_int(dcg_var_t* var, ssize_t value) noexcept nogil
    int c_dcg_var_init_offset(dcg_var_t* var, ssize_t value) noexcept nogil
    int c_dcg_var_init_string(dcg_var_t* var, const char* value) noexcept nogil
    int c_dcg_var_init_ref_raw(dcg_var_t* var, dcg_var_type dtype, const void* ref) noexcept nogil
    int c_dcg_var_init_ref(dcg_var_t* var, const dcg_var_t* src) noexcept nogil
    int c_dcg_var_init_ptr(dcg_var_t* var, void* value) noexcept nogil
    int c_dcg_var_init_dvector(dcg_var_t* var, double* value, size_t n, c_bool copy, allocator_protocol* allocator) noexcept nogil
    int c_dcg_var_init_dmatrix(dcg_var_t* var, double* value, size_t n_rows, size_t n_cols, c_bool row_major, c_bool copy, allocator_protocol* allocator) noexcept nogil

    int c_dcg_var_ref_level(dcg_var_type dtype) noexcept nogil
    c_bool c_dcg_var_is_ref(dcg_var_type dtype) noexcept nogil
    dcg_var_type c_dcg_var_ref_base(dcg_var_type dtype) noexcept nogil

    const char* c_dcg_ret_code_name(dcg_ret_code code) noexcept nogil
    const char* c_dcg_var_type_name(dcg_var_type dtype) noexcept nogil
    c_bool c_dcg_var_is_numeric(const dcg_var_t* var) noexcept nogil
    dcg_var_numeric c_dcg_var_numeric_of(const dcg_var_t* var) noexcept nogil
    c_bool c_dcg_var_is_container(const dcg_var_t* var) noexcept nogil
    c_bool c_dcg_var_is_null(const dcg_var_t* var) noexcept nogil
    c_bool c_dcg_var_is_truthy(const dcg_var_t* var) noexcept nogil
    c_bool c_dcg_var_equals(const dcg_var_t* lhs, const dcg_var_t* rhs) noexcept nogil

    void c_dcg_var_vigilant_abort(const char* getter, dcg_var_type dtype, const char* what) noexcept nogil
    const void* c_dcg_var_ref_slot(const dcg_var_t* var, const char* getter) noexcept nogil
    c_bool c_dcg_var_as_bool(const dcg_var_t* var) noexcept nogil
    double c_dcg_var_as_double(const dcg_var_t* var) noexcept nogil
    ssize_t c_dcg_var_as_int(const dcg_var_t* var) noexcept nogil
    ssize_t c_dcg_var_as_offset(const dcg_var_t* var) noexcept nogil
    const char* c_dcg_var_as_string(const dcg_var_t* var) noexcept nogil
    const void* c_dcg_var_as_ref(const dcg_var_t* var) noexcept nogil
    void* c_dcg_var_as_ptr(const dcg_var_t* var) noexcept nogil
    dcg_d_vector_t* c_dcg_var_as_dvector(const dcg_var_t* var) noexcept nogil
    dcg_d_matrix_t* c_dcg_var_as_dmatrix(const dcg_var_t* var) noexcept nogil
    int c_dcg_var_cast(dcg_var_t* out, const dcg_var_t* var, dcg_var_type dtype) noexcept nogil
    void c_dcg_var_snapshot(dcg_var_t* snapshot, const dcg_var_t* slot) noexcept nogil

    int c_dcg_var_format(const dcg_var_t* var, char* out, size_t cap) noexcept nogil
    int c_dcg_var_print(const dcg_var_t* var, FILE* stream) noexcept nogil


cdef class VarView:
    cdef const dcg_var_t* header

    @staticmethod
    cdef inline VarView c_from_header(const dcg_var_t* var)


cdef void c_dcg_var_pypack(dcg_var_t* out, object value) except *

cdef object c_dcg_var_pyunpack(const dcg_var_t* var)
