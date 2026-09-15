#ifndef C_DCG_BAKE_VAR_H
#define C_DCG_BAKE_VAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

/**
 * @brief Vigilant mode: refuse a read the value's tag cannot answer.
 *
 * Two reads have no correct value to return: one asked for a type the value does
 * not carry, and one through a reference that points at nothing. A 0.0 or a NULL
 * would read as a real payload and let the mistake travel to wherever it
 * surfaces next. In vigilant mode (the default) both print the reader and the
 * tag to stderr and abort, so the mistake lands where it was made. Define this
 * to 0 to have the readers hand back their empty value instead.
 */
#ifndef DCG_VIGILANT
#define DCG_VIGILANT 1
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
    DCG_OK              = 0,    // Success.
    DCG_ERR_INVALID_ARG = -1,   // NULL / out-of-range argument.
    DCG_ERR_INVALID_BUF = -2,   // Buf is not a block start / not zeroed.
    DCG_ERR_OOM         = -3,   // Allocation failed.
    DCG_ERR_NOT_FOUND   = -4,   // Lookup miss.
    DCG_ERR_FULL        = -5,   // Caller-provided buffer is too small.
    DCG_ERR_BAD_CAST    = -6,   // Value tags are not convertible.
    DCG_ERR_FORMAT      = -7,   // Formatting failed.
    DCG_ERR_TYPE        = -8,   // Node/condition kind does not allow the operation.
    DCG_ERR_CYCLE       = -9,   // Operation would create a parent/child cycle.
    DCG_ERR_BUSY        = -10,  // Object is in a state that forbids the operation.
    DCG_ERR_DUPLICATE   = -11,  // The edge condition is already registered on the parent.
    DCG_ERR_EDGE        = -12,  // The edge condition is not acceptable for this parent.
    DCG_ERR_UNRESOLVED  = -13,  // No condition could be inferred for the edge.
    DCG_ERR_RANGE       = -14   // Index outside the container.
} dcg_ret_code;

/**
 * @brief Field layout of a value tag (see dcg_var_type).
 */
typedef enum dcg_var_type_mask {
    VAR_TYPE_BASE_MASK  = 0x00FF,  // Extracts the referred-to tag of a value tag.
    VAR_TYPE_REF_MASK   = 0x0F00,  // Extracts the reference level of a value tag.
    VAR_TYPE_REF_SHIFT  = 8,       // Bits to shift a masked level down to a count.
    VAR_TYPE_REF_LEVEL1 = 0x0100,  // Level of a _REF tag: one hop to the value.
    VAR_TYPE_REF_LEVEL2 = 0x0200   // Level of a _REF_REF tag: two hops to it.
} dcg_var_type_mask;

/**
 * @brief Value tag of a dcg_var_t.
 *
 * The tag is what the evaluator dispatches on; the payload is read through
 * the matching dcg_var_variant member.
 *
 * A tag with no reference bits names a plain value; a tag that has them names a
 * REFERENCE to another value. `as_ref` is the address of the referred-to value at
 * level 1, the address of that address at level 2 - one more star per rung - and
 * the level in VAR_TYPE_REF_MASK is how many there are. The level is a mask and a
 * shift rather than a pair of special cases, so references nest as deep as they
 * are made to: a reference to a double is a _REF holding a double*, a reference
 * to that reference is a _REF_REF holding a double**, and the one after it is
 * level 3. At every level `dtype & VAR_TYPE_BASE_MASK` still names what sits at
 * the end of the walk, which is the value the readers hand back.
 *
 * A reference owns nothing: the value it refers to belongs to whoever set it up,
 * must outlive the reference, must keep the shape the level assumes, and must
 * not be cleared to NULL while a reference to it is read.
 */
typedef enum dcg_var_type {
    VAR_TYPE_RAW_PTR  = 0,   // Opaque pointer payload (as_ptr).
    VAR_TYPE_STRING   = 1,   // NUL-terminated string payload (as_string).
    VAR_TYPE_BOOL     = 2,   // Boolean payload (as_bool).
    VAR_TYPE_DOUBLE   = 3,   // Double payload (as_double).
    VAR_TYPE_INT      = 4,   // Signed integer payload (as_int).
    VAR_TYPE_OFFSET   = 5,   // Signed offset payload (as_offset).
    VAR_TYPE_TIME     = 6,   // Session time payload (as_ptr, or as_time).
    VAR_TYPE_DATE     = 7,   // Session date payload (as_ptr, or as_date).
    VAR_TYPE_DATETIME = 8,   // Session datetime payload (as_ptr, or as_datetime).
    VAR_TYPE_D_VECTOR = 9,   // Contiguous double vector (as_dvector).
    VAR_TYPE_D_MATRIX = 10,  // Contiguous double matrix (as_dmatrix).

    // One hop: as_ref is the address of the slot holding the value.
    VAR_TYPE_RAW_PTR_REF  = VAR_TYPE_REF_LEVEL1 | VAR_TYPE_RAW_PTR,
    VAR_TYPE_STRING_REF   = VAR_TYPE_REF_LEVEL1 | VAR_TYPE_STRING,
    VAR_TYPE_BOOL_REF     = VAR_TYPE_REF_LEVEL1 | VAR_TYPE_BOOL,
    VAR_TYPE_DOUBLE_REF   = VAR_TYPE_REF_LEVEL1 | VAR_TYPE_DOUBLE,
    VAR_TYPE_INT_REF      = VAR_TYPE_REF_LEVEL1 | VAR_TYPE_INT,
    VAR_TYPE_OFFSET_REF   = VAR_TYPE_REF_LEVEL1 | VAR_TYPE_OFFSET,
    VAR_TYPE_TIME_REF     = VAR_TYPE_REF_LEVEL1 | VAR_TYPE_TIME,
    VAR_TYPE_DATE_REF     = VAR_TYPE_REF_LEVEL1 | VAR_TYPE_DATE,
    VAR_TYPE_DATETIME_REF = VAR_TYPE_REF_LEVEL1 | VAR_TYPE_DATETIME,
    VAR_TYPE_D_VECTOR_REF = VAR_TYPE_REF_LEVEL1 | VAR_TYPE_D_VECTOR,
    VAR_TYPE_D_MATRIX_REF = VAR_TYPE_REF_LEVEL1 | VAR_TYPE_D_MATRIX,

    // Two hops: as_ref is the address of the slot holding the one-hop reference.
    VAR_TYPE_RAW_PTR_REF_REF  = VAR_TYPE_REF_LEVEL2 | VAR_TYPE_RAW_PTR,
    VAR_TYPE_STRING_REF_REF   = VAR_TYPE_REF_LEVEL2 | VAR_TYPE_STRING,
    VAR_TYPE_BOOL_REF_REF     = VAR_TYPE_REF_LEVEL2 | VAR_TYPE_BOOL,
    VAR_TYPE_DOUBLE_REF_REF   = VAR_TYPE_REF_LEVEL2 | VAR_TYPE_DOUBLE,
    VAR_TYPE_INT_REF_REF      = VAR_TYPE_REF_LEVEL2 | VAR_TYPE_INT,
    VAR_TYPE_OFFSET_REF_REF   = VAR_TYPE_REF_LEVEL2 | VAR_TYPE_OFFSET,
    VAR_TYPE_TIME_REF_REF     = VAR_TYPE_REF_LEVEL2 | VAR_TYPE_TIME,
    VAR_TYPE_DATE_REF_REF     = VAR_TYPE_REF_LEVEL2 | VAR_TYPE_DATE,
    VAR_TYPE_DATETIME_REF_REF = VAR_TYPE_REF_LEVEL2 | VAR_TYPE_DATETIME,
    VAR_TYPE_D_VECTOR_REF_REF = VAR_TYPE_REF_LEVEL2 | VAR_TYPE_D_VECTOR,
    VAR_TYPE_D_MATRIX_REF_REF = VAR_TYPE_REF_LEVEL2 | VAR_TYPE_D_MATRIX
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
 *
 * A reference tag carries a POINTER to the referred-to slot in `as_ref`, and the
 * level says how many stars it has: the slot holds the value at level 1, the
 * pointer to it at level 2, a pointer to that pointer at level 3. The tag is
 * therefore all a reader needs - the level is the number of hops, the base is
 * what sits at the end of them - and nothing in the payload is ever read as a
 * value header to find the way.
 */
typedef union dcg_var_variant {
    void*           as_ptr;      // VAR_TYPE_RAW_PTR, TIME, DATE, DATETIME.
    const char*     as_string;   // VAR_TYPE_STRING. NOT owned by the var - see c_dcg_var_new_string.
    bool            as_bool;     // VAR_TYPE_BOOL.
    double          as_double;   // VAR_TYPE_DOUBLE.
    ssize_t         as_int;      // VAR_TYPE_INT.
    ssize_t         as_offset;   // VAR_TYPE_OFFSET.
    uint64_t        as_bits;     // Raw 64-bit view of any scalar payload.
    dcg_d_vector_t* as_dvector;  // VAR_TYPE_D_VECTOR. OWNED - child block of the var.
    dcg_d_matrix_t* as_dmatrix;  // VAR_TYPE_D_MATRIX. OWNED - child block of the var.
    const void*     as_ref;      // Any _REF tag: the referred-to slot - T* at level 1, T** at level 2, a star per rung.
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
 *     node's `out` field. Only the container initializers allocate, and only
 *     the shape struct: a string payload stays borrowed, and a container
 *     either wraps the caller's buffer or snapshots it.
 *
 * A container tag points at a dcg_d_vector_t / dcg_d_matrix_t that the var
 * owns as a child block, so the shape travels with the value and one free
 * releases the var, the container and (when the var allocated it) its data.
 * A reference tag owns nothing at all: it is an address of a slot that belongs
 * to whoever set it up, and it reads that slot every time it is read.
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
static inline dcg_var_t*      c_dcg_var_new(allocator_protocol* allocator);
static inline dcg_var_t*      c_dcg_var_new_bool(bool value, allocator_protocol* allocator);
static inline dcg_var_t*      c_dcg_var_new_double(double value, allocator_protocol* allocator);
static inline dcg_var_t*      c_dcg_var_new_int(ssize_t value, allocator_protocol* allocator);
static inline dcg_var_t*      c_dcg_var_new_offset(ssize_t value, allocator_protocol* allocator);
static inline dcg_var_t*      c_dcg_var_new_string(const char* value, allocator_protocol* allocator);
static inline dcg_var_t*      c_dcg_var_new_ptr(void* value, allocator_protocol* allocator);
static inline dcg_var_t*      c_dcg_var_new_dvector(size_t n, allocator_protocol* allocator);
static inline dcg_var_t*      c_dcg_var_new_dmatrix(size_t n_rows, size_t n_cols, bool row_major, allocator_protocol* allocator);
static inline dcg_var_t*      c_dcg_var_new_ref(const dcg_var_t* src, allocator_protocol* allocator);
static inline void            c_dcg_var_free(dcg_var_t* var);

// Population (caller-owned buffer - never allocates)
static inline int             c_dcg_var_init(dcg_var_t* var);
static inline int             c_dcg_var_init_bool(dcg_var_t* var, bool value);
static inline int             c_dcg_var_init_double(dcg_var_t* var, double value);
static inline int             c_dcg_var_init_int(dcg_var_t* var, ssize_t value);
static inline int             c_dcg_var_init_offset(dcg_var_t* var, ssize_t value);
static inline int             c_dcg_var_init_string(dcg_var_t* var, const char* value);
static inline int             c_dcg_var_init_ref_raw(dcg_var_t* var, dcg_var_type dtype, const void* ref);
static inline int             c_dcg_var_init_ref(dcg_var_t* var, const dcg_var_t* src);
static inline int             c_dcg_var_init_ptr(dcg_var_t* var, void* value);
static inline int             c_dcg_var_init_dvector(dcg_var_t* var, double* value, size_t n, bool copy, allocator_protocol* allocator);
static inline int             c_dcg_var_init_dmatrix(dcg_var_t* var, double* value, size_t n_rows, size_t n_cols, bool row_major, bool copy, allocator_protocol* allocator);

// References
static inline int             c_dcg_var_ref_level(dcg_var_type dtype);
static inline bool            c_dcg_var_is_ref(dcg_var_type dtype);
static inline dcg_var_type    c_dcg_var_ref_base(dcg_var_type dtype);

// Introspection
static inline const char*     c_dcg_ret_code_name(dcg_ret_code code);
static inline const char*     c_dcg_var_type_name(dcg_var_type dtype);
static inline bool            c_dcg_var_is_numeric(const dcg_var_t* var);
static inline bool            c_dcg_var_is_container(const dcg_var_t* var);
static inline bool            c_dcg_var_is_null(const dcg_var_t* var);
static inline bool            c_dcg_var_is_truthy(const dcg_var_t* var);
static inline bool            c_dcg_var_equals(const dcg_var_t* lhs, const dcg_var_t* rhs);

// Reading (numeric readers coerce; the rest read their tag only)
static inline void            c_dcg_var_vigilant_abort(const char* getter, dcg_var_type dtype, const char* what);
static inline const void*     c_dcg_var_ref_slot(const dcg_var_t* var, const char* getter);
static inline bool            c_dcg_var_as_bool(const dcg_var_t* var);
static inline double          c_dcg_var_as_double(const dcg_var_t* var);
static inline ssize_t         c_dcg_var_as_int(const dcg_var_t* var);
static inline ssize_t         c_dcg_var_as_offset(const dcg_var_t* var);
static inline const char*     c_dcg_var_as_string(const dcg_var_t* var);
static inline const void*     c_dcg_var_as_ref(const dcg_var_t* var);
static inline void*           c_dcg_var_as_ptr(const dcg_var_t* var);
static inline dcg_d_vector_t* c_dcg_var_as_dvector(const dcg_var_t* var);
static inline dcg_d_matrix_t* c_dcg_var_as_dmatrix(const dcg_var_t* var);
static inline int             c_dcg_var_cast(dcg_var_t* out, const dcg_var_t* var, dcg_var_type dtype);

// Output
static inline int             c_dcg_var_format(const dcg_var_t* var, char* out, size_t cap);
static inline int             c_dcg_var_print(const dcg_var_t* var, FILE* stream);

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
 * @brief Allocate a value that references the CONTENT of another value.
 *
 * The new value owns nothing: the block is its own, the reference inside it is
 * the caller's to keep alive, and c_dcg_var_free() leaves the referenced value
 * untouched.
 *
 * @param src        Value whose content is referred to (must outlive the new value).
 * @param allocator  Allocator for the block (may be NULL).
 * @return The value, or NULL on OOM / a NULL source.
 */
static inline dcg_var_t* c_dcg_var_new_ref(const dcg_var_t* src, allocator_protocol* allocator) {
    if (!src) return NULL;

    dcg_var_t* var = c_dcg_var_new(allocator);
    if (var) (void) c_dcg_var_init_ref(var, src);
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
 * Nothing is copied: the caller keeps the text alive, or hands the value to a
 * node, which copies it into a block of its own. Use c_dcg_var_new_string()
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
 * @brief Populate a caller-owned value with a BORROWED reference to another
 * value, at an explicit level - the primitive behind every reference.
 *
 * `ref` is the pointer the level calls for, and `dtype` is that level: a double*
 * for a _REF to a double, a double** for a _REF_REF to one, one more star per
 * rung after that. c_dcg_var_init_ref() derives both from a value; reach for this
 * when the storage is not a dcg_var_t at all - a caller's buffer, a field of a
 * foreign struct, an element of an array - or when the level is the caller's
 * decision rather than the source's.
 *
 * A reference owns nothing. The storage it points at must outlive it and keep the
 * shape the level assumes.
 *
 * @param var    Value to populate.
 * @param dtype  A reference tag (VAR_TYPE_*_REF / _REF_REF, or any level above).
 * @param ref    Pointer to the referred-to slot of that shape. It may be NULL,
 *               but that is a reference to nothing rather than a value: every
 *               read through it refuses (see c_dcg_var_ref_slot).
 * @return DCG_OK, DCG_ERR_INVALID_ARG or DCG_ERR_TYPE (dtype is not a reference).
 */
static inline int c_dcg_var_init_ref_raw(dcg_var_t* var, dcg_var_type dtype, const void* ref) {
    if (!var) return DCG_ERR_INVALID_ARG;
    if (!c_dcg_var_is_ref(dtype)) return DCG_ERR_TYPE;

    int ret = c_dcg_var_init(var);
    if (ret != DCG_OK) return ret;
    var->dtype        = dtype;
    var->value.as_ref = ref;
    return DCG_OK;
}

/**
 * @brief Populate a caller-owned value with a BORROWED reference to another
 * value - always one level deeper than what it is given, and one more level of
 * POINTER in the payload.
 *
 * The reference is the address of the source's own payload slot, so what lands
 * in `as_ref` is exactly what the level calls for:
 *
 *   - a plain double holds a double, so the address of that slot is a double*,
 *     and the reference is a _REF;
 *   - a double_ref holds a double*, so the address of that slot is a double**,
 *     and the reference is a _REF_REF;
 *   - and one more rung adds one more star, as far as the level field holds.
 *
 * That is what makes this form and c_dcg_var_init_ref_raw() interchangeable:
 * c_dcg_var_init_ref(var, src) builds precisely the var_t that
 * c_dcg_var_init_ref_raw(var, level + 1 | base, &src->value) builds, so there is
 * one representation of a reference and one way to read it.
 *
 * Nothing is copied and nothing is owned: the reference is an address inside the
 * source, which must outlive it - and must keep the shape the level assumes, so
 * a middle rung stays a reference for as long as a rung above it exists.
 *
 * @param var  Value to populate.
 * @param src  Value whose content is referred to (must outlive the reference).
 * @return DCG_OK, or DCG_ERR_INVALID_ARG.
 */
static inline int c_dcg_var_init_ref(dcg_var_t* var, const dcg_var_t* src) {
    if (!var || !src) return DCG_ERR_INVALID_ARG;

    /* One rung above the source, and one more pointer: the address of the slot
     * the source holds its content (or its own reference) in. A level that
     * outgrows VAR_TYPE_REF_MASK is not a reference at all, and init_ref_raw
     * refuses it rather than storing a tag that would read as a plain value. */
    int level = c_dcg_var_ref_level(src->dtype) + 1;

    return c_dcg_var_init_ref_raw(var, (dcg_var_type) ((level << VAR_TYPE_REF_SHIFT) | c_dcg_var_ref_base(src->dtype)), (const void*) &src->value);
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
 * @brief How many hops a tag refers through.
 *
 * The level is read straight out of the tag - a mask and a shift - so it is
 * whatever the tag was built with rather than a pair of known cases: 1 for a
 * _REF, 2 for a _REF_REF, 3 for a reference to one of those, and so on.
 *
 * @param dtype  Value tag.
 * @return The number of hops, or 0 for a plain tag.
 */
static inline int c_dcg_var_ref_level(dcg_var_type dtype) {
    return (int) (((int) dtype & VAR_TYPE_REF_MASK) >> VAR_TYPE_REF_SHIFT);
}

/**
 * @brief Predicate: does the tag name a reference rather than a value?
 *
 * @param dtype  Value tag.
 * @return true for the _REF and _REF_REF tags.
 */
static inline bool c_dcg_var_is_ref(dcg_var_type dtype) {
    return c_dcg_var_ref_level(dtype) != 0;
}

/**
 * @brief The tag a value tag refers to.
 *
 * A plain tag refers to itself, since its reference bits are already clear, so
 * this is also the honest way to ask "what is this really?" of any tag.
 *
 * @param dtype  Value tag.
 * @return The referred-to tag (dtype & VAR_TYPE_BASE_MASK).
 */
static inline dcg_var_type c_dcg_var_ref_base(dcg_var_type dtype) {
    return (dcg_var_type) ((int) dtype & VAR_TYPE_BASE_MASK);
}

/** Display names of the plain tags, indexed by the tag itself. */
static const char* const  DCG_VAR_TYPE_NAMES[] = {"raw_ptr", "string", "bool", "double", "int", "offset", "time", "date", "datetime", "d_vector", "d_matrix"};

/** Display names of the one-hop references, indexed by the tag referred to. */
static const char* const  DCG_VAR_TYPE_REF_NAMES[] = {"raw_ptr_ref", "string_ref", "bool_ref", "double_ref", "int_ref", "offset_ref", "time_ref", "date_ref", "datetime_ref", "d_vector_ref", "d_matrix_ref"};

/** Display names of the two-hop references, indexed by the tag referred to. */
static const char* const  DCG_VAR_TYPE_REF_REF_NAMES[] = {"raw_ptr_ref_ref", "string_ref_ref", "bool_ref_ref", "double_ref_ref", "int_ref_ref", "offset_ref_ref", "time_ref_ref", "date_ref_ref", "datetime_ref_ref", "d_vector_ref_ref", "d_matrix_ref_ref"};

/**
 * @brief Stable display name of a value tag.
 *
 * The tag's level picks the table and its base picks the entry, so a reference
 * is named after what it refers to plus its own suffix - no parsing, no switch
 * over tags that grows with every new type.
 *
 * The tables stop at the two levels that are commonly used: a reference nested
 * deeper keeps the _REF_REF name rather than pretending to count the rungs, and
 * c_dcg_var_ref_level() is where an exact count is read.
 *
 * @param dtype  Value tag.
 * @return Static string; "invalid" for a tag outside the tables.
 */
static inline const char* c_dcg_var_type_name(dcg_var_type dtype) {
    const char* const* names = DCG_VAR_TYPE_NAMES;
    size_t             count = sizeof(DCG_VAR_TYPE_NAMES) / sizeof(char*);

    switch (c_dcg_var_ref_level(dtype)) {
        case 0:
            break;
        case 1:
            names = DCG_VAR_TYPE_REF_NAMES;
            count = sizeof(DCG_VAR_TYPE_REF_NAMES) / sizeof(char*);
            break;
        default:
            names = DCG_VAR_TYPE_REF_REF_NAMES;
            count = sizeof(DCG_VAR_TYPE_REF_REF_NAMES) / sizeof(char*);
            break;
    }

    size_t base = (size_t) ((int) dtype & VAR_TYPE_BASE_MASK);
    return base < count ? names[base] : "invalid";
}

/**
 * @brief Predicate: is the payload a scalar number (int / offset / double)?
 *
 * A reference answers for what it points at, so an evaluator can ask before it
 * follows.
 *
 * @param var  Value to inspect (NULL-safe).
 * @return true when the payload can take part in scalar arithmetic.
 */
static inline bool c_dcg_var_is_numeric(const dcg_var_t* var) {
    if (!var) return false;
    dcg_var_type base = c_dcg_var_ref_base(var->dtype);  // a reference is as numeric as its target
    return base == VAR_TYPE_INT || base == VAR_TYPE_DOUBLE || base == VAR_TYPE_OFFSET;
}

/**
 * @brief Predicate: is the payload a container (double vector / matrix)?
 *
 * @param var  Value to inspect (NULL-safe).
 * @return true for the container tags and the references to them.
 */
static inline bool c_dcg_var_is_container(const dcg_var_t* var) {
    if (!var) return false;
    dcg_var_type base = c_dcg_var_ref_base(var->dtype);
    return base == VAR_TYPE_D_VECTOR || base == VAR_TYPE_D_MATRIX;
}

/**
 * @brief Predicate: is the value absent (no payload of any kind)?
 *
 * Absent is about the VALUE: a live reference whose slot holds NULL is absent,
 * like a NULL string or a vector with no buffer. A reference that points at
 * nothing is not absent, it is broken - the read refuses, and this predicate
 * refuses with it.
 *
 * @param var  Value to inspect (NULL-safe).
 * @return true for a NULL value or a NULL payload.
 */
static inline bool c_dcg_var_is_null(const dcg_var_t* var) {
    if (!var) return true;

    /* The tag behind any reference, or the tag itself: the readers follow the
     * reference, so asking them answers for the value at the end of it. */
    switch (c_dcg_var_ref_base(var->dtype)) {
        case VAR_TYPE_RAW_PTR:
        case VAR_TYPE_TIME:
        case VAR_TYPE_DATE:
        case VAR_TYPE_DATETIME:
            return c_dcg_var_as_ptr(var) == NULL;
        case VAR_TYPE_STRING:
            return c_dcg_var_as_string(var) == NULL;
        case VAR_TYPE_D_VECTOR: {
            const dcg_d_vector_t* vector = c_dcg_var_as_dvector(var);
            return vector == NULL || vector->data == NULL;
        }
        case VAR_TYPE_D_MATRIX: {
            const dcg_d_matrix_t* matrix = c_dcg_var_as_dmatrix(var);
            return matrix == NULL || matrix->data == NULL;
        }
        default:
            return false;  // a scalar is never absent, however empty it reads
    }
}

/**
 * @brief Truthiness of a value, following Python's truth rules.
 *
 * Containers follow the sequence rule too: present and non-empty is truthy,
 * absent or empty is falsy. A reference is judged by what it points at, read at
 * the moment of the call - which is what makes a reference usable directly as a
 * branch condition.
 *
 * @param var  Value to inspect (NULL-safe; NULL is falsy).
 * @return true when the value counts as true in a branch condition.
 */
static inline bool c_dcg_var_is_truthy(const dcg_var_t* var) {
    if (!var) return false;

    switch (c_dcg_var_ref_base(var->dtype)) {
        case VAR_TYPE_BOOL:  // reads as 1 / 0 through the int reader
        case VAR_TYPE_INT:
        case VAR_TYPE_OFFSET:
            return c_dcg_var_as_int(var) != 0;
        case VAR_TYPE_DOUBLE:
            return c_dcg_var_as_double(var) != 0.0;
        case VAR_TYPE_STRING: {
            const char* text = c_dcg_var_as_string(var);
            return text != NULL && text[0] != '\0';
        }
        case VAR_TYPE_D_VECTOR: {
            const dcg_d_vector_t* vector = c_dcg_var_as_dvector(var);
            return vector != NULL && vector->n > 0;
        }
        case VAR_TYPE_D_MATRIX: {
            const dcg_d_matrix_t* matrix = c_dcg_var_as_dmatrix(var);
            return matrix != NULL && matrix->n_rows > 0 && matrix->n_cols > 0;
        }
        case VAR_TYPE_RAW_PTR:
        case VAR_TYPE_TIME:
        case VAR_TYPE_DATE:
        case VAR_TYPE_DATETIME:
            return c_dcg_var_as_ptr(var) != NULL;
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
 * proportional to the payload). A reference compares as the value it points at,
 * read at the moment of the call.
 *
 * @param lhs  Left value (NULL-safe).
 * @param rhs  Right value (NULL-safe).
 * @return true when both tag and payload match.
 */
static inline bool c_dcg_var_equals(const dcg_var_t* lhs, const dcg_var_t* rhs) {
    if (lhs == rhs) return true;
    if (!lhs || !rhs) return false;

    /* The tags are compared behind any reference, because that is what the
     * values really are: a reference to a string and a plain string holding the
     * same text are the same condition, which is what an edge has to match. The
     * readers below follow the reference, so nothing has to be copied to find
     * out. */
    dcg_var_type left  = c_dcg_var_ref_base(lhs->dtype);
    dcg_var_type right = c_dcg_var_ref_base(rhs->dtype);
    if (left != right) return false;

    switch (left) {
        case VAR_TYPE_STRING: {
            const char* a = c_dcg_var_as_string(lhs);
            const char* b = c_dcg_var_as_string(rhs);
            if (a == b) return true;
            if (!a || !b) return false;
            return strcmp(a, b) == 0;
        }
        case VAR_TYPE_BOOL:
            return c_dcg_var_as_bool(lhs) == c_dcg_var_as_bool(rhs);
        case VAR_TYPE_DOUBLE:
            return c_dcg_var_as_double(lhs) == c_dcg_var_as_double(rhs);
        case VAR_TYPE_INT:
        case VAR_TYPE_OFFSET:
            return c_dcg_var_as_int(lhs) == c_dcg_var_as_int(rhs);
        case VAR_TYPE_D_VECTOR: {
            const dcg_d_vector_t* a = c_dcg_var_as_dvector(lhs);
            const dcg_d_vector_t* b = c_dcg_var_as_dvector(rhs);
            if (a == b) return true;
            if (!a || !b || a->n != b->n) return false;
            return a->n == 0 || memcmp(a->data, b->data, a->n * sizeof(double)) == 0;
        }
        case VAR_TYPE_D_MATRIX: {
            const dcg_d_matrix_t* a = c_dcg_var_as_dmatrix(lhs);
            const dcg_d_matrix_t* b = c_dcg_var_as_dmatrix(rhs);
            if (a == b) return true;
            if (!a || !b || a->n_rows != b->n_rows || a->n_cols != b->n_cols || a->row_major != b->row_major) return false;
            size_t total = a->n_rows * a->n_cols;
            return total == 0 || memcmp(a->data, b->data, total * sizeof(double)) == 0;
        }
        default:
            return c_dcg_var_as_ptr(lhs) == c_dcg_var_as_ptr(rhs);
    }
}

// ========== Reading ==========

/**
 * @brief Refuse a read the value's tag cannot answer - loudly, in vigilant mode.
 *
 * Called by a reader that has already found the tag is not one it can read and
 * not a reference to one. In vigilant mode (DCG_VIGILANT, on by default) this
 * names the reader and the tag on stderr and aborts; with DCG_VIGILANT = 0 the
 * message is compiled out and the reader returns its empty value.
 *
 * @param getter  Name of the reader that was asked.
 * @param dtype   Tag of the value it was asked about.
 * @param what    What is wrong with the read, for the message.
 */
static inline void c_dcg_var_vigilant_abort(const char* getter, dcg_var_type dtype, const char* what) {
#if DCG_VIGILANT
    (void) fprintf(stderr, "[DCG] %s: %s (tag %s) - invalid access\n", getter, what, c_dcg_var_type_name(dtype));
    (void) fflush(stderr);
    abort();
#else
    (void) getter;
    (void) dtype;
    (void) what;
#endif
}

/**
 * @brief The slot a live reference holds - refusing one that points at nothing.
 *
 * A reference with a NULL slot is not a value that reads as absent, it is a
 * reference that was never pointed at anything (or was pointed at a slot that
 * has since been cleared): reading through it is a defect, so in vigilant mode
 * this names the reader and refuses before the dereference happens. With
 * DCG_VIGILANT = 0 it answers NULL and the caller reads the empty value.
 *
 * @param var     Reference to read the slot of.
 * @param getter  Name of the reader that is asking, for the message.
 * @return The slot, or NULL when the reference points at nothing.
 */
static inline const void* c_dcg_var_ref_slot(const dcg_var_t* var, const char* getter) {
    if (var->value.as_ref) return var->value.as_ref;

    c_dcg_var_vigilant_abort(getter, var->dtype, "the reference points at nothing");
    return NULL; /* with the vigil off */
}

/**
 * @brief Boolean view of a value.
 *
 * Never refuses: every tag has a truth value, so this is the one reader that can
 * always answer.
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
 * A reference is followed to the number behind it, so an operand that is a
 * reference into the caller's environment evaluates like a plain operand. The
 * number is read out of the slot, never copied out and kept: there is nothing
 * to own, and the next call sees whatever the slot holds then.
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

        /* Level 1 is the common case - an operand reaching into the caller's
         * values outnumbers one holding its own - so it reads the slot right
         * here, and only deeper ladders pay for the walk below. */
        case VAR_TYPE_DOUBLE_REF: {
            const double* at = (const double*) c_dcg_var_ref_slot(var, "c_dcg_var_as_double");
            return at ? *at : 0.0;  // NULL only with the vigil off
        }
        case VAR_TYPE_INT_REF:
        case VAR_TYPE_OFFSET_REF: {
            const ssize_t* at = (const ssize_t*) c_dcg_var_ref_slot(var, "c_dcg_var_as_double");
            return at ? (double) *at : 0.0;
        }
        case VAR_TYPE_BOOL_REF: {
            const bool* at = (const bool*) c_dcg_var_ref_slot(var, "c_dcg_var_as_double");
            return at ? (*at ? 1.0 : 0.0) : 0.0;
        }
        default:
            break;
    }
    /* Not a number by tag: only a reference to one - or to a bool, which counts
     * as one - has an answer to give. */
    dcg_var_type base = c_dcg_var_ref_base(var->dtype);
    if (base != VAR_TYPE_DOUBLE && base != VAR_TYPE_INT && base != VAR_TYPE_OFFSET && base != VAR_TYPE_BOOL) c_dcg_var_vigilant_abort("c_dcg_var_as_double", var->dtype, "cannot read this type");

    if (c_dcg_var_is_ref(var->dtype)) {
        /* Level 2 and deeper: the slot holds the pointer to the number, one star
         * per rung after that, so hop and then read what the tag names. */
        const void* slot = var->value.as_ref;
        for (int level = c_dcg_var_ref_level(var->dtype); level > 1 && slot; level--) slot = *(const void* const*) slot;
        if (!slot) {
            c_dcg_var_vigilant_abort("c_dcg_var_as_double", var->dtype, "the reference points at nothing");
            return 0.0;  // with the vigil off
        }
        switch (base) {
            case VAR_TYPE_DOUBLE:
                return *(const double*) slot;
            case VAR_TYPE_INT:
            case VAR_TYPE_OFFSET:
                return (double) *(const ssize_t*) slot;
            case VAR_TYPE_BOOL:
                return *(const bool*) slot ? 1.0 : 0.0;
            default:
                return 0.0;  // unreachable in vigilant mode
        }
    }
    return 0.0;
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

        /* Level 1 reads the slot right here; only deeper ladders walk below. */
        case VAR_TYPE_INT_REF:
        case VAR_TYPE_OFFSET_REF: {
            const ssize_t* at = (const ssize_t*) c_dcg_var_ref_slot(var, "c_dcg_var_as_int");
            return at ? *at : 0;  // NULL only with the vigil off
        }
        case VAR_TYPE_DOUBLE_REF: {
            const double* at = (const double*) c_dcg_var_ref_slot(var, "c_dcg_var_as_int");
            return at ? (ssize_t) *at : 0;
        }
        case VAR_TYPE_BOOL_REF: {
            const bool* at = (const bool*) c_dcg_var_ref_slot(var, "c_dcg_var_as_int");
            return at ? (*at ? 1 : 0) : 0;
        }
        default:
            break;
    }
    dcg_var_type base = c_dcg_var_ref_base(var->dtype);
    if (base != VAR_TYPE_INT && base != VAR_TYPE_OFFSET && base != VAR_TYPE_DOUBLE && base != VAR_TYPE_BOOL) c_dcg_var_vigilant_abort("c_dcg_var_as_int", var->dtype, "cannot read this type");

    if (c_dcg_var_is_ref(var->dtype)) {
        const void* slot = var->value.as_ref;
        for (int level = c_dcg_var_ref_level(var->dtype); level > 1 && slot; level--) slot = *(const void* const*) slot;
        if (!slot) {
            c_dcg_var_vigilant_abort("c_dcg_var_as_int", var->dtype, "the reference points at nothing");
            return 0;  // with the vigil off
        }
        switch (base) {
            case VAR_TYPE_INT:
            case VAR_TYPE_OFFSET:
                return *(const ssize_t*) slot;
            case VAR_TYPE_DOUBLE:
                return (ssize_t) * (const double*) slot;
            case VAR_TYPE_BOOL:
                return *(const bool*) slot ? 1 : 0;
            default:
                return 0;  // unreachable in vigilant mode
        }
    }
    return 0;
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
 * A reference tag is followed to the string it points at - one hop for a _REF,
 * two for a _REF_REF - so the text read is whatever the slot holds at the
 * moment of the call. Every hop is NULL-checked, so a dangling slot reads as
 * NULL rather than crashing.
 *
 * @param var  Value to read (NULL-safe).
 * @return The string, or NULL when the tag is not string-shaped.
 */
static inline const char* c_dcg_var_as_string(const dcg_var_t* var) {
    if (!var) return NULL;
    if (var->dtype == VAR_TYPE_STRING) return var->value.as_string;
    if (var->dtype == VAR_TYPE_STRING_REF) {  // level 1: the slot holds the text
        const char* const* at = (const char* const*) c_dcg_var_ref_slot(var, "c_dcg_var_as_string");
        return at ? *at : NULL;
    }

    /* Not a string by tag: only a reference to one has an answer to give. */
    if (c_dcg_var_ref_base(var->dtype) != VAR_TYPE_STRING) c_dcg_var_vigilant_abort("c_dcg_var_as_string", var->dtype, "cannot read this type");

    if (c_dcg_var_is_ref(var->dtype)) {
        const void* slot = var->value.as_ref;
        for (int level = c_dcg_var_ref_level(var->dtype); level > 1 && slot; level--) slot = *(const void* const*) slot;
        if (!slot) {
            c_dcg_var_vigilant_abort("c_dcg_var_as_string", var->dtype, "the reference points at nothing");
            return NULL;  // with the vigil off
        }
        return *(const char* const*) slot;
    }
    return NULL;
}

/**
 * @brief The pointer a reference tag holds, exactly as stored (no hop).
 *
 * A T* at level 1, a T** at level 2, one more star per rung - which is what
 * identity checks want: two references are the same reference when they are the
 * same address, whatever it points at. The value at the end of it is what the
 * readers return.
 *
 * @param var  Value to read (NULL-safe).
 * @return The stored pointer, or NULL when the tag is not a reference.
 */
static inline const void* c_dcg_var_as_ref(const dcg_var_t* var) {
    if (!var) return NULL;
    if (!c_dcg_var_is_ref(var->dtype)) c_dcg_var_vigilant_abort("c_dcg_var_as_ref", var->dtype, "cannot read this type");
    return var->value.as_ref;
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

        /* Level 1 of the pointer-shaped tags: the slot holds the pointer. */
        case VAR_TYPE_RAW_PTR_REF:
        case VAR_TYPE_TIME_REF:
        case VAR_TYPE_DATE_REF:
        case VAR_TYPE_DATETIME_REF: {
            void* const* at = (void* const*) c_dcg_var_ref_slot(var, "c_dcg_var_as_ptr");
            return at ? *at : NULL;
        }
        default:
            break;
    }
    /* Not pointer-shaped by tag: only a reference to something that is has an
     * answer to give. */
    dcg_var_type base = c_dcg_var_ref_base(var->dtype);
    if (base != VAR_TYPE_RAW_PTR && base != VAR_TYPE_TIME && base != VAR_TYPE_DATE && base != VAR_TYPE_DATETIME) c_dcg_var_vigilant_abort("c_dcg_var_as_ptr", var->dtype, "cannot read this type");

    if (c_dcg_var_is_ref(var->dtype)) {
        const void* slot = var->value.as_ref;
        for (int level = c_dcg_var_ref_level(var->dtype); level > 1 && slot; level--) slot = *(const void* const*) slot;
        if (!slot) {
            c_dcg_var_vigilant_abort("c_dcg_var_as_ptr", var->dtype, "the reference points at nothing");
            return NULL;  // with the vigil off
        }
        return *(void* const*) slot;
    }
    return NULL;
}

/**
 * @brief Vector view of a value (tag-faithful).
 *
 * @param var  Value to read (NULL-safe).
 * @return The vector, or NULL when the tag is not a vector or a reference to one.
 */
static inline dcg_d_vector_t* c_dcg_var_as_dvector(const dcg_var_t* var) {
    if (!var) return NULL;
    if (var->dtype == VAR_TYPE_D_VECTOR) return var->value.as_dvector;
    if (var->dtype == VAR_TYPE_D_VECTOR_REF) {  // level 1: the slot holds the container
        dcg_d_vector_t* const* at = (dcg_d_vector_t* const*) c_dcg_var_ref_slot(var, "c_dcg_var_as_dvector");
        return at ? *at : NULL;
    }

    /* Not a vector by tag: only a reference to one has an answer to give. */
    if (c_dcg_var_ref_base(var->dtype) != VAR_TYPE_D_VECTOR) c_dcg_var_vigilant_abort("c_dcg_var_as_dvector", var->dtype, "cannot read this type");

    if (c_dcg_var_is_ref(var->dtype)) {
        const void* slot = var->value.as_ref;
        for (int level = c_dcg_var_ref_level(var->dtype); level > 1 && slot; level--) slot = *(const void* const*) slot;
        if (!slot) {
            c_dcg_var_vigilant_abort("c_dcg_var_as_dvector", var->dtype, "the reference points at nothing");
            return NULL;  // with the vigil off
        }
        return *(dcg_d_vector_t* const*) slot;
    }
    return NULL;
}

/**
 * @brief Matrix view of a value (tag-faithful).
 *
 * @param var  Value to read (NULL-safe).
 * @return The matrix, or NULL when the tag is not a matrix or a reference to one.
 */
static inline dcg_d_matrix_t* c_dcg_var_as_dmatrix(const dcg_var_t* var) {
    if (!var) return NULL;
    if (var->dtype == VAR_TYPE_D_MATRIX) return var->value.as_dmatrix;
    if (var->dtype == VAR_TYPE_D_MATRIX_REF) {  // level 1: the slot holds the container
        dcg_d_matrix_t* const* at = (dcg_d_matrix_t* const*) c_dcg_var_ref_slot(var, "c_dcg_var_as_dmatrix");
        return at ? *at : NULL;
    }

    /* Not a matrix by tag: only a reference to one has an answer to give. */
    if (c_dcg_var_ref_base(var->dtype) != VAR_TYPE_D_MATRIX) c_dcg_var_vigilant_abort("c_dcg_var_as_dmatrix", var->dtype, "cannot read this type");

    if (c_dcg_var_is_ref(var->dtype)) {
        const void* slot = var->value.as_ref;
        for (int level = c_dcg_var_ref_level(var->dtype); level > 1 && slot; level--) slot = *(const void* const*) slot;
        if (!slot) {
            c_dcg_var_vigilant_abort("c_dcg_var_as_dmatrix", var->dtype, "the reference points at nothing");
            return NULL;  // with the vigil off
        }
        return *(dcg_d_matrix_t* const*) slot;
    }
    return NULL;
}

/**
 * @brief Retag a value, converting the payload when the tags differ.
 *
 * Supported conversions: within the scalar family (bool / int / offset /
 * double) and from any pointer-shaped tag to RAW_PTR. Strings and containers
 * are never silently reinterpreted: a baked graph keeps its dtypes honest, so
 * a mismatch surfaces at bake time rather than producing 0.0 at eval time.
 *
 * A reference is followed on the way in, so casting a reference to a number
 * gives the number the slot holds. The way back does not exist: a reference tag
 * is a borrowed slot, and only c_dcg_var_init_ref() hands one over.
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

    /* What the source really is: its own tag, or the tag behind its reference.
     * Every branch below reads through the readers, which follow the reference
     * themselves - so nothing is ever resolved into a value of its own. */
    dcg_var_type base = c_dcg_var_ref_base(var->dtype);

    if (var->dtype == dtype) {
        converted = *var;  // Same tag: a copy of the value, a reference included.
    }
    else if (c_dcg_var_is_ref(dtype)) {
        return DCG_ERR_BAD_CAST;  // never synthesized - only init_ref / init_ref_raw hand one over
    }
    else if (dtype == VAR_TYPE_D_VECTOR || dtype == VAR_TYPE_D_MATRIX) {
        if (base != dtype) return DCG_ERR_BAD_CAST;  // a vector is not a matrix
        if (dtype == VAR_TYPE_D_VECTOR) converted.value.as_dvector = c_dcg_var_as_dvector(var);
        else converted.value.as_dmatrix = c_dcg_var_as_dmatrix(var);
    }
    else if (dtype == VAR_TYPE_BOOL) {
        converted.value.as_bool = c_dcg_var_is_truthy(var);
    }
    else if (dtype == VAR_TYPE_INT || dtype == VAR_TYPE_OFFSET || dtype == VAR_TYPE_DOUBLE) {
        if (base == VAR_TYPE_STRING || c_dcg_var_is_container(var)) return DCG_ERR_BAD_CAST;
        if (dtype == VAR_TYPE_INT) converted.value.as_int = c_dcg_var_as_int(var);
        else if (dtype == VAR_TYPE_OFFSET) converted.value.as_offset = c_dcg_var_as_int(var);
        else converted.value.as_double = c_dcg_var_as_double(var);
    }
    else if (dtype == VAR_TYPE_RAW_PTR) {
        if (base == VAR_TYPE_STRING) converted.value.as_ptr = (void*) c_dcg_var_as_string(var);
        else if (base == VAR_TYPE_RAW_PTR || base == VAR_TYPE_TIME || base == VAR_TYPE_DATE || base == VAR_TYPE_DATETIME) converted.value.as_ptr = c_dcg_var_as_ptr(var);
        else return DCG_ERR_BAD_CAST;
    }
    else if (dtype == VAR_TYPE_STRING) {
        converted.value.as_string = c_dcg_var_as_string(var);
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
 * true/false, and containers as their shape with their first elements. A
 * reference renders as the value it points at - c_dcg_var_type_name() is where
 * the storage shows, so a renderer can tell a read-through from a plain value.
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

    /* The tag behind any reference, or the tag itself: the readers follow the
     * reference, so the value at the end of it is what gets rendered. */
    int n = 0;
    switch (c_dcg_var_ref_base(var->dtype)) {
        case VAR_TYPE_BOOL:
            n = snprintf(out, cap, "%s", c_dcg_var_as_bool(var) ? "true" : "false");
            break;
        case VAR_TYPE_INT:
        case VAR_TYPE_OFFSET:
            n = snprintf(out, cap, "%zd", c_dcg_var_as_int(var));
            break;
        case VAR_TYPE_DOUBLE:
            n = snprintf(out, cap, "%g", c_dcg_var_as_double(var));
            break;
        case VAR_TYPE_STRING: {
            const char* text = c_dcg_var_as_string(var);
            n                = text ? snprintf(out, cap, "\"%s\"", text) : snprintf(out, cap, "NULL");
            break;
        }
        case VAR_TYPE_D_VECTOR: {
            const dcg_d_vector_t* vector = c_dcg_var_as_dvector(var);
            n                            = vector ? snprintf(out, cap, "d_vector(n=%zu)", vector->n) : snprintf(out, cap, "d_vector(NULL)");
            break;
        }
        case VAR_TYPE_D_MATRIX: {
            const dcg_d_matrix_t* matrix = c_dcg_var_as_dmatrix(var);
            n                            = matrix ? snprintf(out, cap, "d_matrix(%zux%zu)", matrix->n_rows, matrix->n_cols) : snprintf(out, cap, "d_matrix(NULL)");
            break;
        }
        case VAR_TYPE_RAW_PTR:
        case VAR_TYPE_TIME:
        case VAR_TYPE_DATE:
        case VAR_TYPE_DATETIME:
            n = snprintf(out, cap, "0x%zx", (size_t) (uintptr_t) c_dcg_var_as_ptr(var));
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
