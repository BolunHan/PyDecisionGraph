#ifndef C_DCG_BAKE_VAR_H
#define C_DCG_BAKE_VAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <cbase/allocator_protocol/c_allocator_protocol.h>

// ========== Constants ==========

/**
 * @brief Compile-time switch for the session time/date payload types.
 *
 * The session types (session_time_t / session_date_t / session_datetime_t)
 * live in PyAlgoEngine's exchange_profile header. The bake module is
 * deliberately self-contained, so the dependency is OFF by default and
 * `as_ptr` is the portable accessor for the TIME / DATE / DATETIME tags.
 *
 * Every union member involved is pointer-sized, so enabling this switch
 * never changes the struct ABI - it only adds source-level field names.
 */
#ifndef DCG_VAR_HAS_SESSION_TIME
#define DCG_VAR_HAS_SESSION_TIME 0
#endif

#if DCG_VAR_HAS_SESSION_TIME
#include <algo_engine/exchange_profile/c_ex_profile_base.h>
#endif

/** Capacity of the local formatting buffer used by c_dcg_var_print. */
#ifndef DCG_VAR_STRING_MAXLEN
#define DCG_VAR_STRING_MAXLEN 128
#endif

// ========== Structs ==========

// clang-format off

/**
 * @brief Module-wide return codes for the bake layer (c_var / c_edge / c_node).
 *
 * 0 is success, everything else is negative. Functions that have no
 * exceptional state return their value directly (predicates return bool,
 * counts return size_t) - this enum is only for the error-prone ones.
 */
typedef enum dcg_ret_code {
    DCG_OK               =  0,  // Success.
    DCG_ERR_INVALID_ARG  = -1,  // NULL / out-of-range argument.
    DCG_ERR_INVALID_BUF  = -2,  // Buf is not a block start / not zeroed.
    DCG_ERR_OOM          = -3,  // Allocation failed.
    DCG_ERR_NOT_FOUND    = -4,  // Lookup miss.
    DCG_ERR_FULL         = -5,  // Caller-provided buffer is too small.
    DCG_ERR_BAD_CAST     = -6,  // Value tags are not convertible.
    DCG_ERR_FORMAT       = -7,  // Formatting failed.
    DCG_ERR_TYPE         = -8,  // Node/condition kind does not allow the operation.
    DCG_ERR_CYCLE        = -9,  // Operation would create a parent/child cycle.
    DCG_ERR_BUSY         = -10, // Object is in a state that forbids the operation.
    DCG_ERR_DUPLICATE    = -11, // The edge condition is already registered on the parent.
    DCG_ERR_EDGE         = -12, // The edge condition is not acceptable for this parent.
    DCG_ERR_UNRESOLVED   = -13, // No condition could be inferred for the edge.
    DCG_ERR_RANGE        = -14  // Index outside the container.
} dcg_ret_code;

/**
 * @brief Value tag of a dcg_var_t.
 *
 * The tag is what the evaluator dispatches on; the payload is read through
 * the matching dcg_var_variant member.
 */
typedef enum dcg_var_type {
    VAR_TYPE_RAW_PTR  =  0,  // Opaque pointer payload (as_ptr).
    VAR_TYPE_STRING   =  1,  // NUL-terminated string payload (as_string).
    VAR_TYPE_BOOL     =  2,  // Boolean payload (as_bool).
    VAR_TYPE_DOUBLE   =  3,  // Double payload (as_double).
    VAR_TYPE_INT      =  4,  // Signed integer payload (as_int).
    VAR_TYPE_OFFSET   =  5,  // Signed offset payload (as_offset).
    VAR_TYPE_TIME     =  6,  // Session time payload (as_ptr, or as_time).
    VAR_TYPE_DATE     =  7,  // Session date payload (as_ptr, or as_date).
    VAR_TYPE_DATETIME =  8,  // Session datetime payload (as_ptr, or as_datetime).
    VAR_TYPE_D_VECTOR =  9,  // Contiguous double vector (as_dvector).
    VAR_TYPE_D_MATRIX = 10   // Contiguous double matrix (as_dmatrix).
} dcg_var_type;

/**
 * @brief A contiguous vector of doubles.
 *
 * `data` is a block of its own, allocated as a child of the vector, so freeing
 * the vector (c_dcg_d_vector_free) frees the data with it - the payload can
 * never be orphaned.
 *
 * A var embeds the same struct (dcg_var_t.container), where `data` is the
 * caller's buffer instead; the layout is shared so a var's vector can be read
 * through the very same helpers.
 */
typedef struct dcg_d_vector_t {
    double* data;  // OWNED when standalone - nested block of n doubles. Borrowed inside a var.
    size_t  n;     // Number of doubles.
} dcg_d_vector_t;

/**
 * @brief A contiguous matrix of doubles, with its layout flag.
 *
 * The layout decides how (r, c) maps onto the flat buffer:
 *   - row_major    -> data[r * n_cols + c]
 *   - column-major -> data[c * n_rows + r]
 *
 * As with the vector, `data` is a child block of a standalone matrix and dies
 * with it; inside a var it is the caller's buffer.
 */
typedef struct dcg_d_matrix_t {
    double* data;       // OWNED when standalone - nested block. Borrowed inside a var.
    size_t  n_rows;     // Number of rows.
    size_t  n_cols;     // Number of columns.
    bool    row_major;  // true: row-major, false: column-major.
} dcg_d_matrix_t;

/**
 * @brief Payload storage of a dcg_var_t.
 *
 * Every member is pointer-sized or smaller, so the union is 8 bytes on LP64
 * whatever DCG_VAR_HAS_SESSION_TIME is. A container tag carries a pointer to
 * its shape-carrying struct, which the var OWNS: it is allocated as a child
 * block of the var, so freeing the var frees the container, its data block
 * and the var in one c_ap_free_owned() walk.
 */
typedef union dcg_var_variant {
    void*           as_ptr;       // VAR_TYPE_RAW_PTR, TIME, DATE, DATETIME.
    const char*     as_string;    // VAR_TYPE_STRING. NOT owned by the var - see c_dcg_var_new_string.
    bool            as_bool;      // VAR_TYPE_BOOL.
    double          as_double;    // VAR_TYPE_DOUBLE.
    ssize_t         as_int;       // VAR_TYPE_INT.
    ssize_t         as_offset;    // VAR_TYPE_OFFSET.
    uint64_t        as_bits;      // Raw 64-bit view of any scalar payload.
    dcg_d_vector_t* as_dvector;   // VAR_TYPE_D_VECTOR. OWNED - child block of the var.
    dcg_d_matrix_t* as_dmatrix;   // VAR_TYPE_D_MATRIX. OWNED - child block of the var.
#if DCG_VAR_HAS_SESSION_TIME
    session_time_t*     as_time;      // VAR_TYPE_TIME.
    session_date_t*     as_date;      // VAR_TYPE_DATE.
    session_datetime_t* as_datetime;  // VAR_TYPE_DATETIME.
#endif
} dcg_var_variant;

/**
 * @brief A tagged value - the unit of data flowing through the node graph.
 *
 * Two ways to make one, and the difference is ownership:
 *
 *   - c_dcg_var_new_*()  allocates the var (and, where there is one, its
 *     payload) through the allocator protocol, nesting the payload under the
 *     var - c_dcg_var_free() then releases the whole thing in one call and
 *     nothing can leak.
 *   - c_dcg_var_init_*() populates a buffer the caller owns, most often a
 *     node's `out` field. It never allocates, so a string payload stays
 *     borrowed (a node that must own its string sets
 *     DCG_NODE_FLAG_OWN_STRINGS) and a container wraps a buffer the caller
 *     keeps.
 *
 * A container tag points at a dcg_d_vector_t / dcg_d_matrix_t that the var
 * owns as a child block, so the shape travels with the value and one free
 * releases the var, the container and (when the var allocated it) its data.
 */
typedef struct dcg_var_t {
    dcg_var_type    dtype;  // Active tag; selects the variant member.
    dcg_var_variant value;  // Payload.
} dcg_var_t;

// clang-format on

// ========== Forward Declarations ==========

// Containers (contiguous double payloads with their own metadata)
static inline dcg_d_vector_t* c_dcg_d_vector_new(size_t n, allocator_protocol* allocator);
static inline dcg_d_vector_t* c_dcg_d_vector_new_child(size_t n, allocator_protocol* allocator, const void* parent);
static inline void            c_dcg_d_vector_free(dcg_d_vector_t* vector);
static inline double*         c_dcg_d_vector_data(const dcg_d_vector_t* vector);
static inline size_t          c_dcg_d_vector_size(const dcg_d_vector_t* vector);
static inline double          c_dcg_d_vector_at(const dcg_d_vector_t* vector, size_t index);
static inline int             c_dcg_d_vector_set(dcg_d_vector_t* vector, size_t index, double value);

static inline dcg_d_matrix_t* c_dcg_d_matrix_new(size_t n_rows, size_t n_cols, bool row_major, allocator_protocol* allocator);
static inline dcg_d_matrix_t* c_dcg_d_matrix_new_child(size_t n_rows, size_t n_cols, bool row_major, allocator_protocol* allocator, const void* parent);
static inline void            c_dcg_d_matrix_free(dcg_d_matrix_t* matrix);
static inline double*         c_dcg_d_matrix_data(const dcg_d_matrix_t* matrix);
static inline size_t          c_dcg_d_matrix_rows(const dcg_d_matrix_t* matrix);
static inline size_t          c_dcg_d_matrix_cols(const dcg_d_matrix_t* matrix);
static inline double          c_dcg_d_matrix_at(const dcg_d_matrix_t* matrix, size_t row, size_t col);
static inline int             c_dcg_d_matrix_set(dcg_d_matrix_t* matrix, size_t row, size_t col, double value);

// Lifecycle (allocating - the var owns its payload)
static inline dcg_var_t* c_dcg_var_new(allocator_protocol* allocator);
static inline dcg_var_t* c_dcg_var_new_bool(bool value, allocator_protocol* allocator);
static inline dcg_var_t* c_dcg_var_new_double(double value, allocator_protocol* allocator);
static inline dcg_var_t* c_dcg_var_new_int(ssize_t value, allocator_protocol* allocator);
static inline dcg_var_t* c_dcg_var_new_offset(ssize_t value, allocator_protocol* allocator);
static inline dcg_var_t* c_dcg_var_new_string(const char* value, allocator_protocol* allocator);
static inline dcg_var_t* c_dcg_var_new_ptr(void* value, allocator_protocol* allocator);
static inline dcg_var_t* c_dcg_var_new_dvector(size_t n, allocator_protocol* allocator);
static inline dcg_var_t* c_dcg_var_new_dmatrix(size_t n_rows, size_t n_cols, bool row_major, allocator_protocol* allocator);
static inline void       c_dcg_var_free(dcg_var_t* var);

// Population (caller-owned buffer - never allocates)
static inline int c_dcg_var_init(dcg_var_t* var);
static inline int c_dcg_var_init_bool(dcg_var_t* var, bool value);
static inline int c_dcg_var_init_double(dcg_var_t* var, double value);
static inline int c_dcg_var_init_int(dcg_var_t* var, ssize_t value);
static inline int c_dcg_var_init_offset(dcg_var_t* var, ssize_t value);
static inline int c_dcg_var_init_string(dcg_var_t* var, const char* value);
static inline int c_dcg_var_init_ptr(dcg_var_t* var, void* value);
static inline int c_dcg_var_init_dvector(dcg_var_t* var, double* value, size_t n, bool copy, allocator_protocol* allocator);
static inline int c_dcg_var_init_dmatrix(dcg_var_t* var, double* value, size_t n_rows, size_t n_cols, bool row_major, bool copy, allocator_protocol* allocator);

// Introspection
static inline const char* c_dcg_ret_code_name(dcg_ret_code code);
static inline const char* c_dcg_var_type_name(dcg_var_type dtype);
static inline bool        c_dcg_var_is_numeric(const dcg_var_t* var);
static inline bool        c_dcg_var_is_container(const dcg_var_t* var);
static inline bool        c_dcg_var_is_null(const dcg_var_t* var);
static inline bool        c_dcg_var_is_truthy(const dcg_var_t* var);
static inline bool        c_dcg_var_equals(const dcg_var_t* lhs, const dcg_var_t* rhs);

// Reading (numeric readers coerce; the rest read their tag only)
static inline bool            c_dcg_var_as_bool(const dcg_var_t* var);
static inline double          c_dcg_var_as_double(const dcg_var_t* var);
static inline ssize_t         c_dcg_var_as_int(const dcg_var_t* var);
static inline ssize_t         c_dcg_var_as_offset(const dcg_var_t* var);
static inline const char*     c_dcg_var_as_string(const dcg_var_t* var);
static inline void*           c_dcg_var_as_ptr(const dcg_var_t* var);
static inline dcg_d_vector_t* c_dcg_var_as_dvector(const dcg_var_t* var);
static inline dcg_d_matrix_t* c_dcg_var_as_dmatrix(const dcg_var_t* var);
static inline int             c_dcg_var_cast(dcg_var_t* out, const dcg_var_t* var, dcg_var_type dtype);

// Output
static inline int c_dcg_var_format(const dcg_var_t* var, char* out, size_t cap);
static inline int c_dcg_var_print(const dcg_var_t* var, FILE* stream);

// ========== Containers ==========

/**
 * @brief Allocate a vector of n doubles, zeroed.
 *
 * The data block is allocated as a CHILD of the vector, so it is released
 * together with it and can never be orphaned.
 *
 * @param n          Number of elements.
 * @param allocator  Allocator for both blocks; NULL falls back to the plain heap.
 * @return The vector, or NULL on OOM.
 */
static inline dcg_d_vector_t* c_dcg_d_vector_new(size_t n, allocator_protocol* allocator) {
    return c_dcg_d_vector_new_child(n, allocator, NULL);
}

/**
 * @brief Allocate a vector as a child of another block.
 *
 * @param n          Number of elements.
 * @param allocator  Allocator for both blocks; NULL derives it from the parent.
 * @param parent     Owning block start, or NULL for an independent vector.
 * @return The vector, or NULL on OOM.
 */
static inline dcg_d_vector_t* c_dcg_d_vector_new_child(size_t n, allocator_protocol* allocator, const void* parent) {
    dcg_d_vector_t* vector = (dcg_d_vector_t*) c_ap_alloc_child(sizeof(dcg_d_vector_t), allocator, parent);
    if (!vector) return NULL;

    vector->data = (double*) c_ap_alloc_child(n * sizeof(double), NULL, vector);
    if (!vector->data) {
        c_ap_free_owned(vector);
        return NULL;
    }
    vector->n = n;
    return vector;
}

/**
 * @brief Release a vector and its data.
 *
 * @param vector  Vector to free (NULL-safe).
 */
static inline void c_dcg_d_vector_free(dcg_d_vector_t* vector) {
    if (!vector) return;
    c_ap_free_owned(vector);  // frees the nested data block first
}

/**
 * @brief The vector's contiguous data.
 *
 * @param vector  Vector to inspect (NULL-safe).
 * @return The data block, or NULL.
 */
static inline double* c_dcg_d_vector_data(const dcg_d_vector_t* vector) {
    if (!vector) return NULL;
    return vector->data;
}

/**
 * @brief Number of elements in a vector.
 *
 * @param vector  Vector to inspect (NULL-safe).
 * @return The element count, or 0.
 */
static inline size_t c_dcg_d_vector_size(const dcg_d_vector_t* vector) {
    if (!vector) return 0;
    return vector->n;
}

/**
 * @brief Read one element of a vector.
 *
 * @param vector  Vector to read (NULL-safe).
 * @param index   Element index.
 * @return The element, or 0.0 when the index is out of range.
 */
static inline double c_dcg_d_vector_at(const dcg_d_vector_t* vector, size_t index) {
    if (!vector || !vector->data || index >= vector->n) return 0.0;
    return vector->data[index];
}

/**
 * @brief Write one element of a vector.
 *
 * @param vector  Vector to write (NULL-safe).
 * @param index   Element index.
 * @param value   Value to store.
 * @return DCG_OK, or DCG_ERR_INVALID_ARG / DCG_ERR_RANGE.
 */
static inline int c_dcg_d_vector_set(dcg_d_vector_t* vector, size_t index, double value) {
    if (!vector || !vector->data) return DCG_ERR_INVALID_ARG;
    if (index >= vector->n) return DCG_ERR_RANGE;
    vector->data[index] = value;
    return DCG_OK;
}

/**
 * @brief Allocate an (n_rows x n_cols) matrix of doubles, zeroed.
 *
 * @param n_rows     Number of rows.
 * @param n_cols     Number of columns.
 * @param row_major  true for row-major, false for column-major.
 * @param allocator  Allocator for both blocks; NULL falls back to the plain heap.
 * @return The matrix, or NULL on OOM.
 */
static inline dcg_d_matrix_t* c_dcg_d_matrix_new(size_t n_rows, size_t n_cols, bool row_major, allocator_protocol* allocator) {
    return c_dcg_d_matrix_new_child(n_rows, n_cols, row_major, allocator, NULL);
}

/**
 * @brief Allocate a matrix as a child of another block.
 *
 * @param n_rows     Number of rows.
 * @param n_cols     Number of columns.
 * @param row_major  true for row-major, false for column-major.
 * @param allocator  Allocator for both blocks; NULL derives it from the parent.
 * @param parent     Owning block start, or NULL for an independent matrix.
 * @return The matrix, or NULL on OOM.
 */
static inline dcg_d_matrix_t* c_dcg_d_matrix_new_child(size_t n_rows, size_t n_cols, bool row_major, allocator_protocol* allocator, const void* parent) {
    dcg_d_matrix_t* matrix = (dcg_d_matrix_t*) c_ap_alloc_child(sizeof(dcg_d_matrix_t), allocator, parent);
    if (!matrix) return NULL;

    matrix->data = (double*) c_ap_alloc_child(n_rows * n_cols * sizeof(double), NULL, matrix);
    if (!matrix->data) {
        c_ap_free_owned(matrix);
        return NULL;
    }
    matrix->n_rows    = n_rows;
    matrix->n_cols    = n_cols;
    matrix->row_major = row_major;
    return matrix;
}

/**
 * @brief Release a matrix and its data.
 *
 * @param matrix  Matrix to free (NULL-safe).
 */
static inline void c_dcg_d_matrix_free(dcg_d_matrix_t* matrix) {
    if (!matrix) return;
    c_ap_free_owned(matrix);  // frees the nested data block first
}

/**
 * @brief The matrix's contiguous, row-major data.
 *
 * @param matrix  Matrix to inspect (NULL-safe).
 * @return The data block, or NULL.
 */
static inline double* c_dcg_d_matrix_data(const dcg_d_matrix_t* matrix) {
    if (!matrix) return NULL;
    return matrix->data;
}

/**
 * @brief Number of rows.
 *
 * @param matrix  Matrix to inspect (NULL-safe).
 * @return The row count, or 0.
 */
static inline size_t c_dcg_d_matrix_rows(const dcg_d_matrix_t* matrix) {
    if (!matrix) return 0;
    return matrix->n_rows;
}

/**
 * @brief Number of columns.
 *
 * @param matrix  Matrix to inspect (NULL-safe).
 * @return The column count, or 0.
 */
static inline size_t c_dcg_d_matrix_cols(const dcg_d_matrix_t* matrix) {
    if (!matrix) return 0;
    return matrix->n_cols;
}

/**
 * @brief Read one matrix element.
 *
 * @param matrix  Matrix to read (NULL-safe).
 * @param row     Row index.
 * @param col     Column index.
 * @return The element, or 0.0 when the indices are out of range.
 */
static inline double c_dcg_d_matrix_at(const dcg_d_matrix_t* matrix, size_t row, size_t col) {
    if (!matrix || !matrix->data) return 0.0;
    if (row >= matrix->n_rows || col >= matrix->n_cols) return 0.0;
    size_t index = matrix->row_major ? (row * matrix->n_cols + col) : (col * matrix->n_rows + row);
    return matrix->data[index];
}

/**
 * @brief Write one matrix element.
 *
 * @param matrix  Matrix to write (NULL-safe).
 * @param row     Row index.
 * @param col     Column index.
 * @param value   Value to store.
 * @return DCG_OK, or DCG_ERR_INVALID_ARG / DCG_ERR_RANGE.
 */
static inline int c_dcg_d_matrix_set(dcg_d_matrix_t* matrix, size_t row, size_t col, double value) {
    if (!matrix || !matrix->data) return DCG_ERR_INVALID_ARG;
    if (row >= matrix->n_rows || col >= matrix->n_cols) return DCG_ERR_RANGE;
    size_t index        = matrix->row_major ? (row * matrix->n_cols + col) : (col * matrix->n_rows + row);
    matrix->data[index] = value;
    return DCG_OK;
}

// ========== Lifecycle ==========

/**
 * @brief Allocate a value, tagged RAW_PTR with a NULL payload.
 *
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The value, or NULL on OOM.
 */
static inline dcg_var_t* c_dcg_var_new(allocator_protocol* allocator) {
    dcg_var_t* var = (dcg_var_t*) c_ap_alloc(sizeof(dcg_var_t), allocator);
    if (!var) return NULL;
    c_dcg_var_init(var);
    return var;
}

/**
 * @brief Allocate a bool-tagged value.
 *
 * @param value      Payload.
 * @param allocator  Allocator for the block (may be NULL).
 * @return The value, or NULL on OOM.
 */
static inline dcg_var_t* c_dcg_var_new_bool(bool value, allocator_protocol* allocator) {
    dcg_var_t* var = c_dcg_var_new(allocator);
    if (var) c_dcg_var_init_bool(var, value);
    return var;
}

/**
 * @brief Allocate a double-tagged value.
 *
 * @param value      Payload.
 * @param allocator  Allocator for the block (may be NULL).
 * @return The value, or NULL on OOM.
 */
static inline dcg_var_t* c_dcg_var_new_double(double value, allocator_protocol* allocator) {
    dcg_var_t* var = c_dcg_var_new(allocator);
    if (var) c_dcg_var_init_double(var, value);
    return var;
}

/**
 * @brief Allocate an int-tagged value.
 *
 * @param value      Payload.
 * @param allocator  Allocator for the block (may be NULL).
 * @return The value, or NULL on OOM.
 */
static inline dcg_var_t* c_dcg_var_new_int(ssize_t value, allocator_protocol* allocator) {
    dcg_var_t* var = c_dcg_var_new(allocator);
    if (var) c_dcg_var_init_int(var, value);
    return var;
}

/**
 * @brief Allocate an offset-tagged value.
 *
 * @param value      Payload.
 * @param allocator  Allocator for the block (may be NULL).
 * @return The value, or NULL on OOM.
 */
static inline dcg_var_t* c_dcg_var_new_offset(ssize_t value, allocator_protocol* allocator) {
    dcg_var_t* var = c_dcg_var_new(allocator);
    if (var) c_dcg_var_init_offset(var, value);
    return var;
}

/**
 * @brief Allocate a string-tagged value owning a COPY of the string.
 *
 * The copy is a child block of the value, so c_dcg_var_free() releases both
 * and the text can never be orphaned - this is the owning counterpart of
 * c_dcg_var_init_string(), whose payload stays borrowed.
 *
 * @param value      String to copy (NULL gives an empty string value).
 * @param allocator  Allocator for both blocks (may be NULL).
 * @return The value, or NULL on OOM.
 */
static inline dcg_var_t* c_dcg_var_new_string(const char* value, allocator_protocol* allocator) {
    dcg_var_t* var = c_dcg_var_new(allocator);
    if (!var) return NULL;

    const char* copy = NULL;
    if (value) {
        size_t len = strlen(value);
        char*  buf = (char*) c_ap_alloc_child(len + 1, NULL, var);
        if (!buf) {
            c_ap_free_owned(var);
            return NULL;
        }
        memcpy(buf, value, len + 1);
        copy = buf;
    }

    var->dtype           = VAR_TYPE_STRING;
    var->value.as_string = copy;
    return var;
}

/**
 * @brief Allocate a raw-pointer-tagged value.
 *
 * @param value      Opaque pointer payload (not owned).
 * @param allocator  Allocator for the block (may be NULL).
 * @return The value, or NULL on OOM.
 */
static inline dcg_var_t* c_dcg_var_new_ptr(void* value, allocator_protocol* allocator) {
    dcg_var_t* var = c_dcg_var_new(allocator);
    if (var) c_dcg_var_init_ptr(var, value);
    return var;
}

/**
 * @brief Allocate a vector value owning an n-element double buffer.
 *
 * The value and its data are one nested ownership chain: a single
 * c_dcg_var_free() releases both.
 *
 * @param n          Number of elements.
 * @param allocator  Allocator for the whole chain (may be NULL).
 * @return The value, or NULL on OOM.
 */
static inline dcg_var_t* c_dcg_var_new_dvector(size_t n, allocator_protocol* allocator) {
    dcg_var_t* var = c_dcg_var_new(allocator);
    if (!var) return NULL;

    dcg_d_vector_t* vector = c_dcg_d_vector_new_child(n, NULL, var);
    if (!vector) {
        c_ap_free_owned(var);
        return NULL;
    }

    var->dtype            = VAR_TYPE_D_VECTOR;
    var->value.as_dvector = vector;
    return var;
}

/**
 * @brief Allocate a matrix value owning an (n_rows x n_cols) double matrix.
 *
 * @param n_rows     Number of rows.
 * @param n_cols     Number of columns.
 * @param row_major  true for row-major, false for column-major.
 * @param allocator  Allocator for the whole chain (may be NULL).
 * @return The value, or NULL on OOM.
 */
static inline dcg_var_t* c_dcg_var_new_dmatrix(size_t n_rows, size_t n_cols, bool row_major, allocator_protocol* allocator) {
    dcg_var_t* var = c_dcg_var_new(allocator);
    if (!var) return NULL;

    dcg_d_matrix_t* matrix = c_dcg_d_matrix_new_child(n_rows, n_cols, row_major, NULL, var);
    if (!matrix) {
        c_ap_free_owned(var);
        return NULL;
    }

    var->dtype            = VAR_TYPE_D_MATRIX;
    var->value.as_dmatrix = matrix;
    return var;
}

/**
 * @brief Release a value together with everything it owns.
 *
 * Frees the whole ownership chain - a string copy, a vector or a matrix and
 * its data - then the value block itself. A plain scalar value owns nothing,
 * so this is just its block.
 *
 * @param var  Value to free (NULL-safe). Must be a c_dcg_var_new_*() value,
 *             never one that only went through c_dcg_var_init_*().
 */
static inline void c_dcg_var_free(dcg_var_t* var) {
    if (!var) return;
    c_ap_free_owned(var);
}

// ========== Population ==========

/**
 * @brief Reset a caller-owned value to the empty tag (RAW_PTR / NULL).
 *
 * @param var  Value to populate.
 * @return DCG_OK, or DCG_ERR_INVALID_ARG when var is NULL.
 */
static inline int c_dcg_var_init(dcg_var_t* var) {
    if (!var) return DCG_ERR_INVALID_ARG;
    memset(var, 0, sizeof(dcg_var_t));
    // var->dtype = VAR_TYPE_RAW_PTR;
    return DCG_OK;
}

/**
 * @brief Populate a caller-owned value with a bool.
 *
 * @param var    Value to populate.
 * @param value  Payload.
 * @return DCG_OK, or DCG_ERR_INVALID_ARG.
 */
static inline int c_dcg_var_init_bool(dcg_var_t* var, bool value) {
    int ret = c_dcg_var_init(var);
    if (ret != DCG_OK) return ret;
    var->dtype         = VAR_TYPE_BOOL;
    var->value.as_bool = value;
    return DCG_OK;
}

/**
 * @brief Populate a caller-owned value with a double.
 *
 * @param var    Value to populate.
 * @param value  Payload.
 * @return DCG_OK, or DCG_ERR_INVALID_ARG.
 */
static inline int c_dcg_var_init_double(dcg_var_t* var, double value) {
    int ret = c_dcg_var_init(var);
    if (ret != DCG_OK) return ret;
    var->dtype           = VAR_TYPE_DOUBLE;
    var->value.as_double = value;
    return DCG_OK;
}

/**
 * @brief Populate a caller-owned value with an integer.
 *
 * @param var    Value to populate.
 * @param value  Payload.
 * @return DCG_OK, or DCG_ERR_INVALID_ARG.
 */
static inline int c_dcg_var_init_int(dcg_var_t* var, ssize_t value) {
    int ret = c_dcg_var_init(var);
    if (ret != DCG_OK) return ret;
    var->dtype        = VAR_TYPE_INT;
    var->value.as_int = value;
    return DCG_OK;
}

/**
 * @brief Populate a caller-owned value with an offset.
 *
 * @param var    Value to populate.
 * @param value  Payload.
 * @return DCG_OK, or DCG_ERR_INVALID_ARG.
 */
static inline int c_dcg_var_init_offset(dcg_var_t* var, ssize_t value) {
    int ret = c_dcg_var_init(var);
    if (ret != DCG_OK) return ret;
    var->dtype           = VAR_TYPE_OFFSET;
    var->value.as_offset = value;
    return DCG_OK;
}

/**
 * @brief Populate a caller-owned value with a BORROWED string.
 *
 * Nothing is copied: the caller (or the owning node's
 * DCG_NODE_FLAG_OWN_STRINGS) keeps the text alive. Use c_dcg_var_new_string()
 * when the value itself should own the text.
 *
 * @param var    Value to populate.
 * @param value  NUL-terminated string (may be NULL).
 * @return DCG_OK, or DCG_ERR_INVALID_ARG.
 */
static inline int c_dcg_var_init_string(dcg_var_t* var, const char* value) {
    int ret = c_dcg_var_init(var);
    if (ret != DCG_OK) return ret;
    var->dtype           = VAR_TYPE_STRING;
    var->value.as_string = value;
    return DCG_OK;
}

/**
 * @brief Populate a caller-owned value with a raw pointer.
 *
 * @param var    Value to populate.
 * @param value  Opaque pointer payload (not owned).
 * @return DCG_OK, or DCG_ERR_INVALID_ARG.
 */
static inline int c_dcg_var_init_ptr(dcg_var_t* var, void* value) {
    int ret = c_dcg_var_init(var);
    if (ret != DCG_OK) return ret;
    var->dtype        = VAR_TYPE_RAW_PTR;
    var->value.as_ptr = value;
    return DCG_OK;
}

/**
 * @brief Populate a value with a contiguous vector, wrapped or snapshotted.
 *
 * Either way the shape struct is allocated as a child block of the var, so
 * the var must be a block start - a heap var. The BUFFER differs:
 *
 *   - copy = false  wraps the caller's buffer: borrowed, not adopted, and it
 *     must stay alive and unchanged for as long as the value is read.
 *   - copy = true   takes a snapshot: the buffer is copied into a block owned
 *     by the shape struct, so the value is immune to whatever the caller does
 *     to the source afterwards. It is that copy the var frees, never the
 *     source.
 *
 * @param var        Value to populate. Must be an allocator-protocol block.
 * @param value      Contiguous buffer of n doubles (may be NULL).
 * @param n          Number of doubles.
 * @param copy       true to snapshot the buffer, false to wrap it.
 * @param allocator  Allocator for the shape struct; NULL derives it from var.
 * @return DCG_OK, DCG_ERR_INVALID_ARG, DCG_ERR_INVALID_BUF or DCG_ERR_OOM.
 */
static inline int c_dcg_var_init_dvector(dcg_var_t* var, double* value, size_t n, bool copy, allocator_protocol* allocator) {
    if (!var) return DCG_ERR_INVALID_ARG;
    // Checked before the buf is touched: a refused init leaves it untouched.
    if (!c_ap_is_allocator_buf(var)) return DCG_ERR_INVALID_BUF;

    int ret = c_dcg_var_init(var);
    if (ret != DCG_OK) return ret;

    dcg_d_vector_t* vector = (dcg_d_vector_t*) c_ap_alloc_child(sizeof(dcg_d_vector_t), allocator, var);
    if (!vector) return DCG_ERR_OOM;

    vector->data = value;  // wrapped by default: the caller's buffer, not adopted
    vector->n    = n;

    if (copy && value && n > 0) {
        double* snapshot = (double*) c_ap_alloc_child(n * sizeof(double), NULL, vector);
        if (!snapshot) {
            c_ap_free_owned(vector);  // unlinks from the var; the value stays empty
            return DCG_ERR_OOM;
        }
        memcpy(snapshot, value, n * sizeof(double));
        vector->data = snapshot;  // owned from here on, and freed with the var
    }

    var->dtype            = VAR_TYPE_D_VECTOR;
    var->value.as_dvector = vector;
    return DCG_OK;
}

/**
 * @brief Populate a value with a contiguous matrix, wrapped or snapshotted.
 *
 * As with the vector: the shape struct is a child block of the var (so the
 * var must be a block start), while the BUFFER is either wrapped (borrowed,
 * copy = false) or snapshotted into an owned block (copy = true, immune to
 * later writes to the source). The layout flag is part of the value, so reads
 * honour it (see c_dcg_d_matrix_t).
 *
 * @param var        Value to populate. Must be an allocator-protocol block.
 * @param value      Contiguous buffer of n_rows * n_cols doubles (may be NULL).
 * @param n_rows     Number of rows.
 * @param n_cols     Number of columns.
 * @param row_major  true for row-major, false for column-major.
 * @param copy       true to snapshot the buffer, false to wrap it.
 * @param allocator  Allocator for the shape struct; NULL derives it from var.
 * @return DCG_OK, DCG_ERR_INVALID_ARG, DCG_ERR_INVALID_BUF or DCG_ERR_OOM.
 */
static inline int c_dcg_var_init_dmatrix(dcg_var_t* var, double* value, size_t n_rows, size_t n_cols, bool row_major, bool copy, allocator_protocol* allocator) {
    if (!var) return DCG_ERR_INVALID_ARG;
    // Checked before the buf is touched: a refused init leaves it untouched.
    if (!c_ap_is_allocator_buf(var)) return DCG_ERR_INVALID_BUF;

    int ret = c_dcg_var_init(var);
    if (ret != DCG_OK) return ret;

    dcg_d_matrix_t* matrix = (dcg_d_matrix_t*) c_ap_alloc_child(sizeof(dcg_d_matrix_t), allocator, var);
    if (!matrix) return DCG_ERR_OOM;

    matrix->data      = value;  // wrapped by default: the caller's buffer, not adopted
    matrix->n_rows    = n_rows;
    matrix->n_cols    = n_cols;
    matrix->row_major = row_major;

    size_t total = n_rows * n_cols;
    if (copy && value && total > 0) {
        double* snapshot = (double*) c_ap_alloc_child(total * sizeof(double), NULL, matrix);
        if (!snapshot) {
            c_ap_free_owned(matrix);  // unlinks from the var; the value stays empty
            return DCG_ERR_OOM;
        }
        memcpy(snapshot, value, total * sizeof(double));
        matrix->data = snapshot;  // owned from here on, and freed with the var
    }

    var->dtype            = VAR_TYPE_D_MATRIX;
    var->value.as_dmatrix = matrix;
    return DCG_OK;
}

// ========== Introspection ==========

/**
 * @brief Stable display name of a return code.
 *
 * @param code  Return code.
 * @return Static string; "UNKNOWN" for a code outside the enum.
 */
static inline const char* c_dcg_ret_code_name(dcg_ret_code code) {
    switch (code) {
        case DCG_OK:
            return "OK";
        case DCG_ERR_INVALID_ARG:
            return "INVALID_ARG";
        case DCG_ERR_INVALID_BUF:
            return "INVALID_BUF";
        case DCG_ERR_OOM:
            return "OOM";
        case DCG_ERR_NOT_FOUND:
            return "NOT_FOUND";
        case DCG_ERR_FULL:
            return "FULL";
        case DCG_ERR_BAD_CAST:
            return "BAD_CAST";
        case DCG_ERR_FORMAT:
            return "FORMAT";
        case DCG_ERR_TYPE:
            return "TYPE";
        case DCG_ERR_CYCLE:
            return "CYCLE";
        case DCG_ERR_BUSY:
            return "BUSY";
        case DCG_ERR_DUPLICATE:
            return "DUPLICATE";
        case DCG_ERR_EDGE:
            return "EDGE";
        case DCG_ERR_UNRESOLVED:
            return "UNRESOLVED";
        case DCG_ERR_RANGE:
            return "RANGE";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Stable display name of a value tag.
 *
 * @param dtype  Value tag.
 * @return Static string; "invalid" for an out-of-range tag.
 */
static inline const char* c_dcg_var_type_name(dcg_var_type dtype) {
    switch (dtype) {
        case VAR_TYPE_RAW_PTR:
            return "raw_ptr";
        case VAR_TYPE_STRING:
            return "string";
        case VAR_TYPE_BOOL:
            return "bool";
        case VAR_TYPE_DOUBLE:
            return "double";
        case VAR_TYPE_INT:
            return "int";
        case VAR_TYPE_OFFSET:
            return "offset";
        case VAR_TYPE_TIME:
            return "time";
        case VAR_TYPE_DATE:
            return "date";
        case VAR_TYPE_DATETIME:
            return "datetime";
        case VAR_TYPE_D_VECTOR:
            return "d_vector";
        case VAR_TYPE_D_MATRIX:
            return "d_matrix";
        default:
            return "invalid";
    }
}

/**
 * @brief Predicate: is the payload a scalar number (int / offset / double)?
 *
 * @param var  Value to inspect (NULL-safe).
 * @return true when the payload can take part in scalar arithmetic.
 */
static inline bool c_dcg_var_is_numeric(const dcg_var_t* var) {
    if (!var) return false;
    return var->dtype == VAR_TYPE_INT || var->dtype == VAR_TYPE_DOUBLE || var->dtype == VAR_TYPE_OFFSET;
}

/**
 * @brief Predicate: is the payload a container (double vector / matrix)?
 *
 * @param var  Value to inspect (NULL-safe).
 * @return true for the container tags.
 */
static inline bool c_dcg_var_is_container(const dcg_var_t* var) {
    if (!var) return false;
    return var->dtype == VAR_TYPE_D_VECTOR || var->dtype == VAR_TYPE_D_MATRIX;
}

/**
 * @brief Predicate: is the value absent (no payload of any kind)?
 *
 * @param var  Value to inspect (NULL-safe).
 * @return true for a NULL value or a NULL payload.
 */
static inline bool c_dcg_var_is_null(const dcg_var_t* var) {
    if (!var) return true;
    switch (var->dtype) {
        case VAR_TYPE_RAW_PTR:
        case VAR_TYPE_TIME:
        case VAR_TYPE_DATE:
        case VAR_TYPE_DATETIME:
            return var->value.as_ptr == NULL;
        case VAR_TYPE_STRING:
            return var->value.as_string == NULL;
        case VAR_TYPE_D_VECTOR:
            return var->value.as_dvector == NULL || var->value.as_dvector->data == NULL;
        case VAR_TYPE_D_MATRIX:
            return var->value.as_dmatrix == NULL || var->value.as_dmatrix->data == NULL;
        default:
            return false;
    }
}

/**
 * @brief Truthiness of a value, following Python's truth rules.
 *
 * Containers follow the sequence rule too: present and non-empty is truthy,
 * absent or empty is falsy.
 *
 * @param var  Value to inspect (NULL-safe; NULL is falsy).
 * @return true when the value counts as true in a branch condition.
 */
static inline bool c_dcg_var_is_truthy(const dcg_var_t* var) {
    if (!var) return false;
    switch (var->dtype) {
        case VAR_TYPE_BOOL:
            return var->value.as_bool;
        case VAR_TYPE_INT:
            return var->value.as_int != 0;
        case VAR_TYPE_OFFSET:
            return var->value.as_offset != 0;
        case VAR_TYPE_DOUBLE:
            return var->value.as_double != 0.0;
        case VAR_TYPE_STRING:
            return var->value.as_string != NULL && var->value.as_string[0] != '\0';
        case VAR_TYPE_D_VECTOR:
            return var->value.as_dvector != NULL && var->value.as_dvector->n > 0;
        case VAR_TYPE_D_MATRIX:
            return var->value.as_dmatrix != NULL && var->value.as_dmatrix->n_rows > 0 && var->value.as_dmatrix->n_cols > 0;
        case VAR_TYPE_RAW_PTR:
        case VAR_TYPE_TIME:
        case VAR_TYPE_DATE:
        case VAR_TYPE_DATETIME:
            return var->value.as_ptr != NULL;
        default:
            return false;
    }
}

/**
 * @brief Equality of two values, comparing tag first, then payload.
 *
 * Two values of different tags are never equal. Strings compare by content,
 * pointers by address, and the containers element-wise (which is what makes
 * a container usable as a branch condition - and what makes the comparison
 * proportional to the payload).
 *
 * @param lhs  Left value (NULL-safe).
 * @param rhs  Right value (NULL-safe).
 * @return true when both tag and payload match.
 */
static inline bool c_dcg_var_equals(const dcg_var_t* lhs, const dcg_var_t* rhs) {
    if (lhs == rhs) return true;
    if (!lhs || !rhs) return false;
    if (lhs->dtype != rhs->dtype) return false;

    switch (lhs->dtype) {
        case VAR_TYPE_STRING: {
            const char* a = lhs->value.as_string;
            const char* b = rhs->value.as_string;
            if (a == b) return true;
            if (!a || !b) return false;
            return strcmp(a, b) == 0;
        }
        case VAR_TYPE_BOOL:
            return lhs->value.as_bool == rhs->value.as_bool;
        case VAR_TYPE_DOUBLE:
            return lhs->value.as_double == rhs->value.as_double;
        case VAR_TYPE_INT:
            return lhs->value.as_int == rhs->value.as_int;
        case VAR_TYPE_OFFSET:
            return lhs->value.as_offset == rhs->value.as_offset;
        case VAR_TYPE_D_VECTOR: {
            const dcg_d_vector_t* a = lhs->value.as_dvector;
            const dcg_d_vector_t* b = rhs->value.as_dvector;
            if (a == b) return true;
            if (!a || !b || a->n != b->n) return false;
            return a->n == 0 || memcmp(a->data, b->data, a->n * sizeof(double)) == 0;
        }
        case VAR_TYPE_D_MATRIX: {
            const dcg_d_matrix_t* a = lhs->value.as_dmatrix;
            const dcg_d_matrix_t* b = rhs->value.as_dmatrix;
            if (a == b) return true;
            if (!a || !b || a->n_rows != b->n_rows || a->n_cols != b->n_cols || a->row_major != b->row_major) return false;
            size_t total = a->n_rows * a->n_cols;
            return total == 0 || memcmp(a->data, b->data, total * sizeof(double)) == 0;
        }
        default:
            return lhs->value.as_ptr == rhs->value.as_ptr;
    }
}

// ========== Reading ==========

/**
 * @brief Boolean view of a value.
 *
 * @param var  Value to read (NULL-safe).
 * @return c_dcg_var_is_truthy() of the value.
 */
static inline bool c_dcg_var_as_bool(const dcg_var_t* var) {
    return c_dcg_var_is_truthy(var);
}

/**
 * @brief Numeric view of a value (coercing).
 *
 * @param var  Value to read (NULL-safe).
 * @return The payload as double; 0.0 for a non-numeric or NULL value.
 */
static inline double c_dcg_var_as_double(const dcg_var_t* var) {
    if (!var) return 0.0;
    switch (var->dtype) {
        case VAR_TYPE_DOUBLE:
            return var->value.as_double;
        case VAR_TYPE_INT:
            return (double) var->value.as_int;
        case VAR_TYPE_OFFSET:
            return (double) var->value.as_offset;
        case VAR_TYPE_BOOL:
            return var->value.as_bool ? 1.0 : 0.0;
        default:
            return 0.0;
    }
}

/**
 * @brief Integer view of a value (coercing, doubles truncate toward zero).
 *
 * @param var  Value to read (NULL-safe).
 * @return The payload as ssize_t; 0 for a non-numeric or NULL value.
 */
static inline ssize_t c_dcg_var_as_int(const dcg_var_t* var) {
    if (!var) return 0;
    switch (var->dtype) {
        case VAR_TYPE_INT:
            return var->value.as_int;
        case VAR_TYPE_OFFSET:
            return var->value.as_offset;
        case VAR_TYPE_DOUBLE:
            return (ssize_t) var->value.as_double;
        case VAR_TYPE_BOOL:
            return var->value.as_bool ? 1 : 0;
        default:
            return 0;
    }
}

/**
 * @brief Offset view of a value (coercing).
 *
 * @param var  Value to read (NULL-safe).
 * @return The payload as ssize_t; 0 for a non-numeric or NULL value.
 */
static inline ssize_t c_dcg_var_as_offset(const dcg_var_t* var) {
    return c_dcg_var_as_int(var);
}

/**
 * @brief String view of a value (tag-faithful, no coercion).
 *
 * @param var  Value to read (NULL-safe).
 * @return The string payload, or NULL when the tag is not a string.
 */
static inline const char* c_dcg_var_as_string(const dcg_var_t* var) {
    if (!var || var->dtype != VAR_TYPE_STRING) return NULL;
    return var->value.as_string;
}

/**
 * @brief Raw-pointer view of a value (tag-faithful).
 *
 * @param var  Value to read (NULL-safe).
 * @return The pointer payload, or NULL when the tag is not pointer-shaped.
 */
static inline void* c_dcg_var_as_ptr(const dcg_var_t* var) {
    if (!var) return NULL;
    switch (var->dtype) {
        case VAR_TYPE_RAW_PTR:
        case VAR_TYPE_TIME:
        case VAR_TYPE_DATE:
        case VAR_TYPE_DATETIME:
            return var->value.as_ptr;
        default:
            return NULL;
    }
}

/**
 * @brief Vector view of a value (tag-faithful).
 *
 * @param var  Value to read (NULL-safe).
 * @return The vector, or NULL when the tag is not a vector.
 */
static inline dcg_d_vector_t* c_dcg_var_as_dvector(const dcg_var_t* var) {
    if (!var || var->dtype != VAR_TYPE_D_VECTOR) return NULL;
    return var->value.as_dvector;
}

/**
 * @brief Matrix view of a value (tag-faithful).
 *
 * @param var  Value to read (NULL-safe).
 * @return The matrix, or NULL when the tag is not a matrix.
 */
static inline dcg_d_matrix_t* c_dcg_var_as_dmatrix(const dcg_var_t* var) {
    if (!var || var->dtype != VAR_TYPE_D_MATRIX) return NULL;
    return var->value.as_dmatrix;
}

/**
 * @brief Retag a value, converting the payload when the tags differ.
 *
 * Supported conversions: within the scalar family (bool / int / offset /
 * double) and from any pointer-shaped tag to RAW_PTR. Strings and containers
 * are never silently reinterpreted: a baked graph keeps its dtypes honest, so
 * a mismatch surfaces at bake time rather than producing 0.0 at eval time.
 *
 * @param out    Receives the converted value (NULL-safe: skipped when NULL).
 * @param var    Value to convert.
 * @param dtype  Target tag.
 * @return DCG_OK on success, DCG_ERR_INVALID_ARG / DCG_ERR_BAD_CAST otherwise.
 */
static inline int c_dcg_var_cast(dcg_var_t* out, const dcg_var_t* var, dcg_var_type dtype) {
    if (!var) return DCG_ERR_INVALID_ARG;

    dcg_var_t converted;
    (void) c_dcg_var_init(&converted);
    converted.dtype = dtype;

    if (var->dtype == dtype) {
        converted = *var;
    }
    else if (dtype == VAR_TYPE_BOOL) {
        converted.value.as_bool = c_dcg_var_is_truthy(var);
    }
    else if (dtype == VAR_TYPE_INT) {
        if (var->dtype == VAR_TYPE_STRING || c_dcg_var_is_container(var)) return DCG_ERR_BAD_CAST;
        converted.value.as_int = c_dcg_var_as_int(var);
    }
    else if (dtype == VAR_TYPE_OFFSET) {
        if (var->dtype == VAR_TYPE_STRING || c_dcg_var_is_container(var)) return DCG_ERR_BAD_CAST;
        converted.value.as_offset = c_dcg_var_as_int(var);
    }
    else if (dtype == VAR_TYPE_DOUBLE) {
        if (var->dtype == VAR_TYPE_STRING || c_dcg_var_is_container(var)) return DCG_ERR_BAD_CAST;
        converted.value.as_double = c_dcg_var_as_double(var);
    }
    else if (dtype == VAR_TYPE_RAW_PTR) {
        converted.value.as_ptr = var->value.as_ptr;
    }
    else if (dtype == VAR_TYPE_STRING) {
        if (var->dtype != VAR_TYPE_STRING) return DCG_ERR_BAD_CAST;
        converted.value.as_string = var->value.as_string;
    }
    else {
        return DCG_ERR_BAD_CAST;
    }

    if (out) *out = converted;
    return DCG_OK;
}

// ========== Output ==========

/**
 * @brief Format a value into a caller-provided buffer (renderer support).
 *
 * Strings render quoted, doubles with %g, pointers as 0x addresses, bools as
 * true/false, and containers as their shape with their first elements.
 *
 * @param var  Value to format (NULL-safe: renders "(null)").
 * @param out  Destination buffer.
 * @param cap  Capacity of out.
 * @return Number of characters written (excluding NUL), or DCG_ERR_INVALID_ARG
 *         / DCG_ERR_FORMAT.
 */
static inline int c_dcg_var_format(const dcg_var_t* var, char* out, size_t cap) {
    if (!out || cap == 0) return DCG_ERR_INVALID_ARG;
    if (!var) {
        int n = snprintf(out, cap, "(null)");
        return n < 0 ? DCG_ERR_FORMAT : n;
    }

    int n = 0;
    switch (var->dtype) {
        case VAR_TYPE_BOOL:
            n = snprintf(out, cap, "%s", var->value.as_bool ? "true" : "false");
            break;
        case VAR_TYPE_INT:
            n = snprintf(out, cap, "%zd", var->value.as_int);
            break;
        case VAR_TYPE_OFFSET:
            n = snprintf(out, cap, "%zd", var->value.as_offset);
            break;
        case VAR_TYPE_DOUBLE:
            n = snprintf(out, cap, "%g", var->value.as_double);
            break;
        case VAR_TYPE_STRING:
            n = var->value.as_string ? snprintf(out, cap, "\"%s\"", var->value.as_string) : snprintf(out, cap, "NULL");
            break;
        case VAR_TYPE_D_VECTOR: {
            const dcg_d_vector_t* vector = var->value.as_dvector;
            n                            = vector ? snprintf(out, cap, "d_vector(n=%zu)", vector->n) : snprintf(out, cap, "d_vector(NULL)");
            break;
        }
        case VAR_TYPE_D_MATRIX: {
            const dcg_d_matrix_t* matrix = var->value.as_dmatrix;
            n                            = matrix ? snprintf(out, cap, "d_matrix(%zux%zu)", matrix->n_rows, matrix->n_cols) : snprintf(out, cap, "d_matrix(NULL)");
            break;
        }
        case VAR_TYPE_RAW_PTR:
        case VAR_TYPE_TIME:
        case VAR_TYPE_DATE:
        case VAR_TYPE_DATETIME:
            n = snprintf(out, cap, "0x%zx", (size_t) (uintptr_t) var->value.as_ptr);
            break;
        default:
            n = snprintf(out, cap, "<invalid:%d>", (int) var->dtype);
            break;
    }

    return n < 0 ? DCG_ERR_FORMAT : n;
}

/**
 * @brief Print a value to a stream (rendering convenience).
 *
 * @param var     Value to print (NULL-safe).
 * @param stream  Destination stream.
 * @return Number of characters written, or DCG_ERR_INVALID_ARG / DCG_ERR_FORMAT.
 */
static inline int c_dcg_var_print(const dcg_var_t* var, FILE* stream) {
    if (!stream) return DCG_ERR_INVALID_ARG;
    char buf[DCG_VAR_STRING_MAXLEN];
    int  n = c_dcg_var_format(var, buf, sizeof(buf));
    if (n < 0) return n;
    return fprintf(stream, "%s", buf);
}

#endif  // C_DCG_BAKE_VAR_H
