/*
 * c_var.h - value layer: containers, allocation-free population, the owning
 * constructors, references, truthiness, equality, casts and output.
 */

#include <decision_graph/decision_tree/bake/c_var.h>

#if DCG_VIGILANT
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "test_util.h"

static void test_init_population(void) {
    dcg_var_t var;

    DCG_CHECK_INT(c_dcg_var_init(&var), DCG_OK);
    DCG_CHECK_INT(var.dtype, VAR_TYPE_RAW_PTR);
    DCG_CHECK(var.value.as_ptr == NULL);
    DCG_CHECK(c_dcg_var_is_null(&var));

    DCG_CHECK_INT(c_dcg_var_init_bool(&var, true), DCG_OK);
    DCG_CHECK_INT(var.dtype, VAR_TYPE_BOOL);
    DCG_CHECK(var.value.as_bool);

    /* Populating an already-used value must not leave anything stale behind. */
    DCG_CHECK_INT(c_dcg_var_init_string(&var, "text"), DCG_OK);
    DCG_CHECK_INT(var.dtype, VAR_TYPE_STRING);
    DCG_CHECK_STR(var.value.as_string, "text");
    DCG_CHECK_INT(var.value.as_bits, (uint64_t) (uintptr_t) "text"); /* the payload is just the pointer */

    DCG_CHECK_INT(c_dcg_var_init_double(&var, 1.5), DCG_OK);
    DCG_CHECK(var.value.as_double == 1.5);
    DCG_CHECK_INT(c_dcg_var_init_int(&var, -7), DCG_OK);
    DCG_CHECK_INT(var.value.as_int, -7);
    DCG_CHECK_INT(c_dcg_var_init_offset(&var, 3), DCG_OK);
    DCG_CHECK_INT(var.value.as_offset, 3);
    DCG_CHECK_INT(c_dcg_var_init_ptr(&var, (void*) 0x1234), DCG_OK);
    DCG_CHECK(var.value.as_ptr == (void*) 0x1234);

    DCG_CHECK_INT(c_dcg_var_init(NULL), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_var_init_int(NULL, 1), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_var_init_string(NULL, "x"), DCG_ERR_INVALID_ARG);
}

static void test_owning_constructors(void) {
    dcg_var_t* var = c_dcg_var_new(NULL);
    DCG_CHECK(var != NULL);
    DCG_CHECK_INT(var->dtype, VAR_TYPE_RAW_PTR);
    c_dcg_var_free(var);

    var = c_dcg_var_new_bool(true, NULL);
    DCG_CHECK(var != NULL && c_dcg_var_as_bool(var));
    c_dcg_var_free(var);

    var = c_dcg_var_new_double(2.5, NULL);
    DCG_CHECK(var != NULL && var->value.as_double == 2.5);
    c_dcg_var_free(var);

    var = c_dcg_var_new_int(-3, NULL);
    DCG_CHECK(var != NULL && var->value.as_int == -3);
    c_dcg_var_free(var);

    var = c_dcg_var_new_offset(9, NULL);
    DCG_CHECK(var != NULL && var->value.as_offset == 9);
    c_dcg_var_free(var);

    var = c_dcg_var_new_ptr((void*) 0xabc, NULL);
    DCG_CHECK(var != NULL && c_dcg_var_as_ptr(var) == (void*) 0xabc);
    c_dcg_var_free(var);

    c_dcg_var_free(NULL); /* NULL-safe */
}

static void test_owned_string(void) {
    /* The value owns a COPY: freeing the value releases the text with it, and
     * the caller's original stays untouched. */
    char       original[] = "own me";
    dcg_var_t* var        = c_dcg_var_new_string(original, NULL);

    DCG_CHECK(var != NULL);
    DCG_CHECK_INT(var->dtype, VAR_TYPE_STRING);
    DCG_CHECK_STR(c_dcg_var_as_string(var), "own me");
    DCG_CHECK(c_dcg_var_as_string(var) != original); /* a copy, not the caller's pointer */

    original[0] = 'X'; /* the copy must not follow */
    DCG_CHECK_STR(c_dcg_var_as_string(var), "own me");

    c_dcg_var_free(var); /* frees the copy too - LSan would report a leak otherwise */

    /* A NULL source gives an empty (NULL) string value, still freeable. */
    var = c_dcg_var_new_string(NULL, NULL);
    DCG_CHECK(var != NULL);
    DCG_CHECK_INT(var->dtype, VAR_TYPE_STRING);
    DCG_CHECK(c_dcg_var_as_string(var) == NULL);
    DCG_CHECK(c_dcg_var_is_null(var));
    c_dcg_var_free(var);
}

static void test_vectors(void) {
    dcg_d_vector_t* vector = c_dcg_d_vector_new(4, NULL);
    DCG_CHECK(vector != NULL);
    DCG_CHECK_INT(c_dcg_d_vector_size(vector), 4);
    DCG_CHECK(c_dcg_d_vector_data(vector) != NULL);
    DCG_CHECK(c_dcg_d_vector_data(vector)[0] == 0.0); /* zeroed on allocation */

    DCG_CHECK_INT(c_dcg_d_vector_set(vector, 2, 6.25), DCG_OK);
    DCG_CHECK(c_dcg_d_vector_at(vector, 2) == 6.25);
    DCG_CHECK(c_dcg_d_vector_at(vector, 3) == 0.0);

    /* Range errors, and NULL-safety throughout. */
    DCG_CHECK_INT(c_dcg_d_vector_set(vector, 4, 1.0), DCG_ERR_RANGE);
    DCG_CHECK_INT(c_dcg_d_vector_set(NULL, 0, 1.0), DCG_ERR_INVALID_ARG);
    DCG_CHECK(c_dcg_d_vector_at(vector, 99) == 0.0);
    DCG_CHECK(c_dcg_d_vector_at(NULL, 0) == 0.0);
    DCG_CHECK_INT(c_dcg_d_vector_size(NULL), 0);
    DCG_CHECK(c_dcg_d_vector_data(NULL) == NULL);
    c_dcg_d_vector_free(NULL);

    c_dcg_d_vector_free(vector); /* frees the nested data block with it */
}

static void test_matrices(void) {
    dcg_d_matrix_t* matrix = c_dcg_d_matrix_new(2, 3, true, NULL);
    DCG_CHECK(matrix != NULL);
    DCG_CHECK_INT(c_dcg_d_matrix_rows(matrix), 2);
    DCG_CHECK_INT(c_dcg_d_matrix_cols(matrix), 3);
    DCG_CHECK(c_dcg_d_matrix_data(matrix) != NULL);

    /* Row-major layout: element (r, c) is at r * n_cols + c. */
    DCG_CHECK_INT(c_dcg_d_matrix_set(matrix, 1, 2, 7.5), DCG_OK);
    DCG_CHECK(c_dcg_d_matrix_at(matrix, 1, 2) == 7.5);
    DCG_CHECK(c_dcg_d_matrix_data(matrix)[1 * 3 + 2] == 7.5);
    DCG_CHECK(c_dcg_d_matrix_at(matrix, 0, 0) == 0.0);

    DCG_CHECK_INT(c_dcg_d_matrix_set(matrix, 2, 0, 1.0), DCG_ERR_RANGE);
    DCG_CHECK_INT(c_dcg_d_matrix_set(matrix, 0, 3, 1.0), DCG_ERR_RANGE);
    DCG_CHECK_INT(c_dcg_d_matrix_set(NULL, 0, 0, 1.0), DCG_ERR_INVALID_ARG);
    DCG_CHECK(c_dcg_d_matrix_at(matrix, 9, 9) == 0.0);
    DCG_CHECK(c_dcg_d_matrix_at(NULL, 0, 0) == 0.0);

    c_dcg_d_matrix_free(matrix);

    /* Column-major: (r, c) sits at c * n_rows + r instead. Element (1, 1) is
     * the case that tells the two layouts apart - (1, 2) would land on index 5
     * either way in a 2x3. */
    matrix = c_dcg_d_matrix_new(2, 3, false, NULL);
    DCG_CHECK(matrix != NULL);
    DCG_CHECK(!matrix->row_major);
    DCG_CHECK_INT(c_dcg_d_matrix_set(matrix, 1, 1, 7.5), DCG_OK);
    DCG_CHECK(c_dcg_d_matrix_at(matrix, 1, 1) == 7.5);
    DCG_CHECK(c_dcg_d_matrix_data(matrix)[1 * 2 + 1] == 7.5); /* c * n_rows + r */
    DCG_CHECK(c_dcg_d_matrix_data(matrix)[1 * 3 + 1] == 0.0); /* the row-major slot stays untouched */
    c_dcg_d_matrix_free(matrix);

    /* A shape with a zero side is legal and owns an empty (but valid) block. */
    matrix = c_dcg_d_matrix_new(0, 5, true, NULL);
    DCG_CHECK(matrix != NULL);
    DCG_CHECK_INT(c_dcg_d_matrix_rows(matrix), 0);
    DCG_CHECK(c_dcg_d_matrix_at(matrix, 0, 0) == 0.0);
    c_dcg_d_matrix_free(matrix);
}

static void test_container_values(void) {
    /* The owning constructors nest value -> vector -> data, so one free
     * releases the whole chain. */
    dcg_var_t* var = c_dcg_var_new_dvector(3, NULL);
    DCG_CHECK(var != NULL);
    DCG_CHECK_INT(var->dtype, VAR_TYPE_D_VECTOR);
    DCG_CHECK(c_dcg_var_is_container(var));

    dcg_d_vector_t* vector = c_dcg_var_as_dvector(var);
    DCG_CHECK(vector != NULL);
    DCG_CHECK_INT(c_dcg_d_vector_size(vector), 3);
    DCG_CHECK_INT(c_dcg_d_vector_set(vector, 0, 1.0), DCG_OK);
    DCG_CHECK(c_dcg_d_vector_at(c_dcg_var_as_dvector(var), 0) == 1.0);

#if !DCG_VIGILANT
    /* The other tag readers refuse it: with the vigil off they hand back the
     * empty value, and the default build aborts instead (test_vigilant_abort). */
    DCG_CHECK(c_dcg_var_as_dmatrix(var) == NULL);
    DCG_CHECK(c_dcg_var_as_string(var) == NULL);
    DCG_CHECK(c_dcg_var_as_ptr(var) == NULL);
#endif
    c_dcg_var_free(var);

    var = c_dcg_var_new_dmatrix(2, 2, true, NULL);
    DCG_CHECK(var != NULL);
    DCG_CHECK_INT(var->dtype, VAR_TYPE_D_MATRIX);
    DCG_CHECK_INT(c_dcg_d_matrix_set(c_dcg_var_as_dmatrix(var), 1, 1, 4.0), DCG_OK);
    DCG_CHECK(c_dcg_d_matrix_at(c_dcg_var_as_dmatrix(var), 1, 1) == 4.0);
#if !DCG_VIGILANT
    DCG_CHECK(c_dcg_var_as_dvector(var) == NULL);
#endif
    c_dcg_var_free(var);

    /* Attaching a caller's buffer: the buffer is wrapped, not copied and not
     * adopted, while the shape struct is allocated as a child of the var. */
    double     raw[4]   = {1.0, 2.0, 3.0, 4.0};
    dcg_var_t* attached = c_dcg_var_new(NULL);

    DCG_CHECK_INT(c_dcg_var_init_dvector(attached, raw, 4, false, NULL), DCG_OK);
    DCG_CHECK_INT(attached->dtype, VAR_TYPE_D_VECTOR);
    DCG_CHECK(c_dcg_var_as_dvector(attached)->data == raw);
    DCG_CHECK_INT(c_dcg_var_as_dvector(attached)->n, 4);
    raw[0] = 9.0; /* the var reads the caller's buffer, it did not copy it */
    DCG_CHECK(c_dcg_d_vector_at(c_dcg_var_as_dvector(attached), 0) == 9.0);

    /* The same buffer, seen as a matrix with an explicit layout. */
    DCG_CHECK_INT(c_dcg_var_init_dmatrix(attached, raw, 2, 2, true, false, NULL), DCG_OK);
    DCG_CHECK_INT(attached->dtype, VAR_TYPE_D_MATRIX);
    DCG_CHECK(c_dcg_var_as_dmatrix(attached)->data == raw);
    DCG_CHECK_INT(c_dcg_var_as_dmatrix(attached)->n_rows, 2);
    DCG_CHECK_INT(c_dcg_var_as_dmatrix(attached)->n_cols, 2);
    DCG_CHECK(c_dcg_var_as_dmatrix(attached)->row_major);

    /* A NULL buffer is a legal (empty) container value. */
    DCG_CHECK_INT(c_dcg_var_init_dmatrix(attached, NULL, 0, 0, false, false, NULL), DCG_OK);
    DCG_CHECK(c_dcg_var_is_null(attached));
    DCG_CHECK(!c_dcg_var_is_truthy(attached));

    /* Freeing the var releases the shape structs it allocated; the caller's
     * buffer is NOT behind an allocator-protocol child link, so it survives
     * untouched - which is exactly what LSan will have verified by now. */
    c_dcg_var_free(attached);

    /* A var that is not a block start cannot own a container, and says so
     * instead of reaching for an allocator header that is not there. */
    dcg_var_t embedded;
    (void) c_dcg_var_init(&embedded);
    DCG_CHECK_INT(c_dcg_var_init_dvector(&embedded, raw, 4, false, NULL), DCG_ERR_INVALID_BUF);
    DCG_CHECK_INT(embedded.dtype, VAR_TYPE_RAW_PTR); /* left untouched */
    DCG_CHECK_INT(c_dcg_var_init_dmatrix(&embedded, raw, 2, 2, true, false, NULL), DCG_ERR_INVALID_BUF);
}

static void test_type_names(void) {
    DCG_CHECK_STR(c_dcg_var_type_name(VAR_TYPE_RAW_PTR), "raw_ptr");
    DCG_CHECK_STR(c_dcg_var_type_name(VAR_TYPE_STRING), "string");
    DCG_CHECK_STR(c_dcg_var_type_name(VAR_TYPE_BOOL), "bool");
    DCG_CHECK_STR(c_dcg_var_type_name(VAR_TYPE_DOUBLE), "double");
    DCG_CHECK_STR(c_dcg_var_type_name(VAR_TYPE_INT), "int");
    DCG_CHECK_STR(c_dcg_var_type_name(VAR_TYPE_OFFSET), "offset");
    DCG_CHECK_STR(c_dcg_var_type_name(VAR_TYPE_TIME), "time");
    DCG_CHECK_STR(c_dcg_var_type_name(VAR_TYPE_D_VECTOR), "d_vector");
    DCG_CHECK_STR(c_dcg_var_type_name(VAR_TYPE_D_MATRIX), "d_matrix");
    DCG_CHECK_STR(c_dcg_var_type_name(VAR_TYPE_STRING_REF), "string_ref");
    DCG_CHECK_STR(c_dcg_var_type_name(VAR_TYPE_STRING_REF_REF), "string_ref_ref");
    DCG_CHECK_STR(c_dcg_var_type_name(VAR_TYPE_DOUBLE_REF), "double_ref");
    DCG_CHECK_STR(c_dcg_var_type_name(VAR_TYPE_D_MATRIX_REF_REF), "d_matrix_ref_ref");
    DCG_CHECK_STR(c_dcg_var_type_name((dcg_var_type) 99), "invalid");
}

static void test_reference_tags(void) {
    /* The tag carries the level and the target: the level says how many hops,
     * the base says what is at the end of them. */
    DCG_CHECK(!c_dcg_var_is_ref(VAR_TYPE_DOUBLE));
    DCG_CHECK(c_dcg_var_is_ref(VAR_TYPE_DOUBLE_REF));
    DCG_CHECK(c_dcg_var_is_ref(VAR_TYPE_DOUBLE_REF_REF));
    DCG_CHECK_INT(c_dcg_var_ref_level(VAR_TYPE_DOUBLE), 0);
    DCG_CHECK_INT(c_dcg_var_ref_level(VAR_TYPE_DOUBLE_REF), 1);
    DCG_CHECK_INT(c_dcg_var_ref_level(VAR_TYPE_DOUBLE_REF_REF), 2);
    DCG_CHECK_INT(c_dcg_var_ref_base(VAR_TYPE_STRING_REF), VAR_TYPE_STRING);
    DCG_CHECK_INT(c_dcg_var_ref_base(VAR_TYPE_STRING_REF_REF), VAR_TYPE_STRING);
    DCG_CHECK_INT(c_dcg_var_ref_base(VAR_TYPE_D_VECTOR_REF), VAR_TYPE_D_VECTOR);
    DCG_CHECK_INT(c_dcg_var_ref_base(VAR_TYPE_D_MATRIX), VAR_TYPE_D_MATRIX); /* a plain tag is its own base */

    /* Every tag has both reference forms, and both point back at it. */
    const dcg_var_type bases[] = {VAR_TYPE_RAW_PTR, VAR_TYPE_STRING, VAR_TYPE_BOOL, VAR_TYPE_DOUBLE, VAR_TYPE_INT, VAR_TYPE_OFFSET, VAR_TYPE_TIME, VAR_TYPE_DATE, VAR_TYPE_DATETIME, VAR_TYPE_D_VECTOR, VAR_TYPE_D_MATRIX};

    for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); i++) {
        for (int level = 1; level <= 2; level++) {
            dcg_var_type tag = (dcg_var_type) ((level == 1 ? VAR_TYPE_REF_LEVEL1 : VAR_TYPE_REF_LEVEL2) | bases[i]);

            DCG_CHECK_INT(c_dcg_var_ref_base(tag), bases[i]);
            DCG_CHECK_INT(c_dcg_var_ref_level(tag), level);
            DCG_CHECK(c_dcg_var_is_ref(tag));
            DCG_CHECK(strcmp(c_dcg_var_type_name(tag), "invalid") != 0); /* every tag has a name */
        }
    }
}

static void test_reference_raw(void) {
    /* The primitive stores the pointer it is given, which is what storage that
     * is not a value needs: a caller's local, a buffer element, a foreign
     * struct's field. */
    double    number = 1.5;
    dcg_var_t ref;

    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_DOUBLE_REF, &number), DCG_OK);
    DCG_CHECK_INT(ref.dtype, VAR_TYPE_DOUBLE_REF);
    DCG_CHECK(c_dcg_var_as_ref(&ref) == (const void*) &number); /* the pointer, as given */
    DCG_CHECK(c_dcg_var_as_double(&ref) == 1.5);
    DCG_CHECK(c_dcg_var_is_numeric(&ref));
    DCG_CHECK(c_dcg_var_is_truthy(&ref));

    number = 2.5; /* read time, not init time */
    DCG_CHECK(c_dcg_var_as_double(&ref) == 2.5);
    DCG_CHECK(c_dcg_var_as_int(&ref) == 2);

    number = 0.0;
    DCG_CHECK(!c_dcg_var_is_truthy(&ref));

    /* A level-2 reference holds the address of the pointer, which is how a slot
     * that is re-pointed keeps being followed. */
    double  first  = 10.0;
    double  second = 20.0;
    double* slot   = &first;

    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_DOUBLE_REF_REF, &slot), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_ref_level(ref.dtype), 2);
    DCG_CHECK(c_dcg_var_as_double(&ref) == 10.0);
    slot = &second;
    DCG_CHECK(c_dcg_var_as_double(&ref) == 20.0);
    second = 21.0;
    DCG_CHECK(c_dcg_var_as_double(&ref) == 21.0);

    /* A string slot, one level deep and two. */
    const char* text = "first";
    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_STRING_REF, &text), DCG_OK);
    DCG_CHECK_STR(c_dcg_var_as_string(&ref), "first");
    DCG_CHECK(!c_dcg_var_is_numeric(&ref));
    text = "second";
    DCG_CHECK_STR(c_dcg_var_as_string(&ref), "second");

    const char*  inner = "deep";
    const char** outer = &inner;

    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_STRING_REF_REF, &outer), DCG_OK);
    DCG_CHECK_STR(c_dcg_var_as_string(&ref), "deep");
    inner = "deeper";
    DCG_CHECK_STR(c_dcg_var_as_string(&ref), "deeper");

    /* A container slot: whatever pointer the slot holds, followed. */
    double          data[2]  = {1.0, 2.0};
    dcg_var_t*      vector   = c_dcg_var_new(NULL);
    dcg_d_vector_t* vec_slot = NULL;

    (void) c_dcg_var_init_dvector(vector, data, 2, false, NULL);
    vec_slot = c_dcg_var_as_dvector(vector);

    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_D_VECTOR_REF, &vec_slot), DCG_OK);
    DCG_CHECK(c_dcg_var_is_container(&ref));
    DCG_CHECK(c_dcg_var_as_dvector(&ref) == vec_slot);
    DCG_CHECK_INT(c_dcg_d_vector_size(c_dcg_var_as_dvector(&ref)), 2);
    /* The slot is cleared while the reference stays live: what sits at the end
     * of it is gone, which is an ordinary absent value - not a broken reference,
     * so the read answers as it always would. */
    vec_slot = NULL;
    DCG_CHECK(c_dcg_var_as_ref(&ref) == (const void*) &vec_slot);
    DCG_CHECK(c_dcg_var_as_dvector(&ref) == NULL);
    DCG_CHECK(c_dcg_var_is_null(&ref));
    c_dcg_var_free(vector); /* the reference owned nothing to release */

    /* A level the tag cannot hold is not a reference at all, and is refused
     * rather than stored as a tag that would read as a plain value. */
    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_DOUBLE, &number), DCG_ERR_TYPE);
    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, (dcg_var_type) (16 << VAR_TYPE_REF_SHIFT), &number), DCG_ERR_TYPE);
    DCG_CHECK_INT(c_dcg_var_init_ref_raw(NULL, VAR_TYPE_DOUBLE_REF, &number), DCG_ERR_INVALID_ARG);

    /* A reference that points at nothing is a defect, not a value: reading
     * through it refuses (test_vigilant_abort forks for exactly that). The
     * reference itself stays readable, whichever rung is missing. */
    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_STRING_REF, NULL), DCG_OK);
    DCG_CHECK(c_dcg_var_as_ref(&ref) == NULL);
    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_DOUBLE_REF_REF, NULL), DCG_OK);
    DCG_CHECK(c_dcg_var_as_ref(&ref) == NULL);

#if !DCG_VIGILANT
    DCG_CHECK(c_dcg_var_as_double(&ref) == 0.0); /* the empty value, with the vigil off */
    DCG_CHECK(!c_dcg_var_is_null(&ref));         /* a scalar is never absent, reference or not */
    DCG_CHECK(!c_dcg_var_is_truthy(&ref));
    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_STRING_REF, NULL), DCG_OK);
    DCG_CHECK(c_dcg_var_as_string(&ref) == NULL);
    DCG_CHECK(c_dcg_var_is_null(&ref));
#endif

    DCG_CHECK(c_dcg_var_as_ref(NULL) == NULL);
    DCG_CHECK(c_dcg_var_is_null(NULL));
}

static void test_reference_to_value(void) {
    /* The derived form: a reference to another value, one level deeper than what
     * it is given and one more star in the payload. */
    dcg_var_t value;
    dcg_var_t ref;

    DCG_CHECK_INT(c_dcg_var_init_double(&value, 1.5), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_init_ref(&ref, &value), DCG_OK);
    DCG_CHECK_INT(ref.dtype, VAR_TYPE_DOUBLE_REF);                   /* a plain value becomes a _REF */
    DCG_CHECK(c_dcg_var_as_ref(&ref) == (const void*) &value.value); /* a double*, into the source */
    DCG_CHECK(c_dcg_var_as_double(&ref) == 1.5);

    DCG_CHECK_INT(c_dcg_var_init_double(&value, 2.5), DCG_OK); /* the slot is stable, so this is live */
    DCG_CHECK(c_dcg_var_as_double(&ref) == 2.5);

    /* A reference to a reference: the address of the pointer, so a double**, and
     * the value sits two hops away. */
    dcg_var_t ref_ref;
    DCG_CHECK_INT(c_dcg_var_init_ref(&ref_ref, &ref), DCG_OK);
    DCG_CHECK_INT(ref_ref.dtype, VAR_TYPE_DOUBLE_REF_REF);
    DCG_CHECK(c_dcg_var_as_ref(&ref_ref) == (const void*) &ref.value);
    DCG_CHECK(c_dcg_var_as_double(&ref_ref) == 2.5);
    dcg_t_trace_var("a value (double)", &value);
    dcg_t_trace_var("a _REF to it", &ref);
    dcg_t_trace_var("a _REF_REF to that", &ref_ref);

    /* ... and one more rung, which is level 3. The name tables stop at the two
     * common levels; the count is read from the tag. */
    dcg_var_t deep;
    DCG_CHECK_INT(c_dcg_var_init_ref(&deep, &ref_ref), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_ref_level(deep.dtype), 3);
    DCG_CHECK_INT(c_dcg_var_ref_base(deep.dtype), VAR_TYPE_DOUBLE);
    DCG_CHECK_STR(c_dcg_var_type_name(deep.dtype), "double_ref_ref");
    DCG_CHECK_INT(c_dcg_var_init_double(&value, 4.5), DCG_OK);
    DCG_CHECK(c_dcg_var_as_double(&deep) == 4.5); /* three hops down, still live */

    dcg_var_t deeper;
    DCG_CHECK_INT(c_dcg_var_init_ref(&deeper, &deep), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_ref_level(deeper.dtype), 4);
    DCG_CHECK(c_dcg_var_as_double(&deeper) == 4.5);

    /* A string source, walked the same way - and its own reference, since a rung
     * is only a rung for as long as the value under it keeps its shape. */
    dcg_var_t text_src;
    dcg_var_t text_ref;
    DCG_CHECK_INT(c_dcg_var_init_string(&text_src, "first"), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_init_ref(&text_ref, &text_src), DCG_OK);
    DCG_CHECK_INT(text_ref.dtype, VAR_TYPE_STRING_REF);
    DCG_CHECK_STR(c_dcg_var_as_string(&text_ref), "first");
    DCG_CHECK_INT(c_dcg_var_init_string(&text_src, "second"), DCG_OK);
    DCG_CHECK_STR(c_dcg_var_as_string(&text_ref), "second");

    /* The two constructors agree: what init_ref derives is exactly what
     * init_ref_raw takes, so a reference can be rebuilt from its address. */
    dcg_var_t same;
    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&same, VAR_TYPE_DOUBLE_REF_REF, &ref.value), DCG_OK);
    DCG_CHECK_INT(same.dtype, ref_ref.dtype);
    DCG_CHECK(c_dcg_var_as_ref(&same) == c_dcg_var_as_ref(&ref_ref));
    DCG_CHECK(c_dcg_var_as_double(&same) == 4.5);

    /* The allocating form: the block belongs to the value, the reference does
     * not, so freeing it leaves the source exactly as it was. */
    dcg_var_t* heap = c_dcg_var_new_ref(&value, NULL);
    DCG_CHECK(heap != NULL);
    DCG_CHECK_INT(heap->dtype, VAR_TYPE_DOUBLE_REF);
    DCG_CHECK(c_dcg_var_as_double(heap) == 4.5);
    c_dcg_var_free(heap);
    DCG_CHECK(c_dcg_var_as_double(&value) == 4.5); /* the source outlived the reference */

    /* A missing source is refused, not guessed at. */
    DCG_CHECK_INT(c_dcg_var_init_ref(&ref, NULL), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_var_init_ref(NULL, &value), DCG_ERR_INVALID_ARG);
    DCG_CHECK(c_dcg_var_new_ref(NULL, NULL) == NULL);
}

static void test_reference_level_one(void) {
    /* A level-1 reference of every type reads through the level-1 path: the
     * value is one dereference away, whatever it holds. */
    dcg_var_t value;
    dcg_var_t ref;

    DCG_CHECK_INT(c_dcg_var_init_int(&value, -7), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_init_ref(&ref, &value), DCG_OK);
    DCG_CHECK_INT(ref.dtype, VAR_TYPE_INT_REF);
    DCG_CHECK_INT(c_dcg_var_as_int(&ref), -7);
    DCG_CHECK(c_dcg_var_as_double(&ref) == -7.0);
    dcg_t_trace_var("int value", &value);
    dcg_t_trace_var("int_ref reading it", &ref);

    DCG_CHECK_INT(c_dcg_var_init_offset(&value, 3), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_init_ref(&ref, &value), DCG_OK);
    DCG_CHECK_INT(ref.dtype, VAR_TYPE_OFFSET_REF);
    DCG_CHECK_INT(c_dcg_var_as_offset(&ref), 3);
    DCG_CHECK_INT(c_dcg_var_as_int(&ref), 3);

    DCG_CHECK_INT(c_dcg_var_init_bool(&value, true), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_init_ref(&ref, &value), DCG_OK);
    DCG_CHECK_INT(ref.dtype, VAR_TYPE_BOOL_REF);
    DCG_CHECK(c_dcg_var_as_bool(&ref));
    DCG_CHECK(c_dcg_var_as_double(&ref) == 1.0);
    DCG_CHECK_INT(c_dcg_var_as_int(&ref), 1);

    /* A raw pointer, and a session-time slot, which is pointer-shaped too. */
    int marker = 0;
    DCG_CHECK_INT(c_dcg_var_init_ptr(&value, &marker), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_init_ref(&ref, &value), DCG_OK);
    DCG_CHECK_INT(ref.dtype, VAR_TYPE_RAW_PTR_REF);
    DCG_CHECK(c_dcg_var_as_ptr(&ref) == &marker);

    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_TIME_REF, &value.value), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_ref_base(ref.dtype), VAR_TYPE_TIME);
    DCG_CHECK(c_dcg_var_as_ptr(&ref) == &marker);

    /* A matrix reference carries the shape with it. */
    double     data[4] = {1.0, 2.0, 3.0, 4.0};
    dcg_var_t* matrix  = c_dcg_var_new(NULL);
    dcg_var_t  matrix_ref;

    DCG_CHECK_INT(c_dcg_var_init_dmatrix(matrix, data, 2, 2, true, false, NULL), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_init_ref(&matrix_ref, matrix), DCG_OK);
    DCG_CHECK_INT(matrix_ref.dtype, VAR_TYPE_D_MATRIX_REF);
    DCG_CHECK(c_dcg_var_as_dmatrix(&matrix_ref) == c_dcg_var_as_dmatrix(matrix));
    DCG_CHECK(c_dcg_d_matrix_at(c_dcg_var_as_dmatrix(&matrix_ref), 1, 1) == 4.0);
    DCG_CHECK(c_dcg_var_equals(&matrix_ref, matrix));
    c_dcg_var_free(matrix); /* the reference owned nothing to release */

#if !DCG_VIGILANT
    /* A slot that points at nothing reads as the empty value, in the same path -
     * the default build refuses it (test_vigilant_abort). */
    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_INT_REF, NULL), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_as_int(&ref), 0);
    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_OFFSET_REF, NULL), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_as_offset(&ref), 0);
    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_BOOL_REF, NULL), DCG_OK);
    DCG_CHECK(!c_dcg_var_as_bool(&ref));
    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_DOUBLE_REF, NULL), DCG_OK);
    DCG_CHECK(c_dcg_var_as_double(&ref) == 0.0);
    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_STRING_REF, NULL), DCG_OK);
    DCG_CHECK(c_dcg_var_as_string(&ref) == NULL);
    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_RAW_PTR_REF, NULL), DCG_OK);
    DCG_CHECK(c_dcg_var_as_ptr(&ref) == NULL);
    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_TIME_REF, NULL), DCG_OK);
    DCG_CHECK(c_dcg_var_as_ptr(&ref) == NULL);
    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_D_VECTOR_REF, NULL), DCG_OK);
    DCG_CHECK(c_dcg_var_as_dvector(&ref) == NULL);
    DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, VAR_TYPE_D_MATRIX_REF, NULL), DCG_OK);
    DCG_CHECK(c_dcg_var_as_dmatrix(&ref) == NULL);
#else
    /* With the vigil on, the reference is what is left to read. */
    const dcg_var_type dead_tags[] = {VAR_TYPE_INT_REF,  VAR_TYPE_OFFSET_REF, VAR_TYPE_BOOL_REF,   VAR_TYPE_DOUBLE_REF, VAR_TYPE_STRING_REF,
                                      VAR_TYPE_RAW_PTR_REF, VAR_TYPE_TIME_REF, VAR_TYPE_D_VECTOR_REF, VAR_TYPE_D_MATRIX_REF};

    for (size_t i = 0; i < sizeof(dead_tags) / sizeof(dead_tags[0]); i++) {
        DCG_CHECK_INT(c_dcg_var_init_ref_raw(&ref, dead_tags[i], NULL), DCG_OK);
        DCG_CHECK(c_dcg_var_as_ref(&ref) == NULL);
        DCG_CHECK(c_dcg_var_is_ref(ref.dtype));
    }
#endif
}

static void test_reference_consumers(void) {
    /* Everything that reads a value reads through a reference: that is what
     * makes a reference usable anywhere a plain value is. */
    dcg_var_t number;
    dcg_var_t text;
    dcg_var_t number_ref;
    dcg_var_t text_ref;
    dcg_var_t out;

    (void) c_dcg_var_init_double(&number, 3.0);
    (void) c_dcg_var_init_string(&text, "abc");
    DCG_CHECK_INT(c_dcg_var_init_ref(&number_ref, &number), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_init_ref(&text_ref, &text), DCG_OK);

    /* Equality compares what is pointed at, so a condition holding a reference
     * matches a plain incoming value. */
    dcg_var_t three = dcg_t_var_double(3.0);
    dcg_var_t abc   = dcg_t_var_string("abc");

    DCG_CHECK(c_dcg_var_equals(&number_ref, &three));
    DCG_CHECK(c_dcg_var_equals(&text_ref, &abc));
    DCG_CHECK(!c_dcg_var_equals(&number_ref, &abc)); /* the tags still differ */
    (void) c_dcg_var_init_double(&number, 4.0);
    DCG_CHECK(!c_dcg_var_equals(&number_ref, &three));
    DCG_CHECK(c_dcg_var_equals(&number_ref, &number)); /* and it follows the source */

    /* A cast follows the reference; a cast TO a reference tag is refused, since
     * a reference is a borrowed address and not something a value can become. */
    DCG_CHECK_INT(c_dcg_var_cast(&out, &number_ref, VAR_TYPE_INT), DCG_OK);
    DCG_CHECK_INT(out.value.as_int, 4);
    DCG_CHECK_INT(c_dcg_var_cast(&out, &text_ref, VAR_TYPE_STRING), DCG_OK);
    DCG_CHECK_STR(out.value.as_string, "abc");
    DCG_CHECK_INT(c_dcg_var_cast(&out, &number_ref, VAR_TYPE_DOUBLE_REF), DCG_OK); /* same tag: a copy */
    DCG_CHECK(c_dcg_var_as_ref(&out) == (const void*) &number.value);
    DCG_CHECK_INT(c_dcg_var_cast(&out, &three, VAR_TYPE_DOUBLE_REF), DCG_ERR_BAD_CAST);
    DCG_CHECK_INT(c_dcg_var_cast(&out, &number_ref, VAR_TYPE_DOUBLE_REF_REF), DCG_ERR_BAD_CAST);

    /* Formatting renders the value behind it; the type name is where the
     * storage shows. */
    char buf[64];
    DCG_CHECK(c_dcg_var_format(&number_ref, buf, sizeof(buf)) > 0);
    DCG_CHECK_STR(buf, "4");
    DCG_CHECK(c_dcg_var_format(&text_ref, buf, sizeof(buf)) > 0);
    DCG_CHECK_STR(buf, "\"abc\"");

    /* A container is read through a reference like anything else - and is still
     * the container, for the predicates and for a cast. */
    double     data[2] = {1.0, 2.0};
    dcg_var_t* vector  = c_dcg_var_new(NULL);
    dcg_var_t  vector_ref;

    (void) c_dcg_var_init_dvector(vector, data, 2, false, NULL);
    DCG_CHECK_INT(c_dcg_var_init_ref(&vector_ref, vector), DCG_OK);
    DCG_CHECK(c_dcg_var_is_container(&vector_ref));
    DCG_CHECK(c_dcg_var_as_dvector(&vector_ref) == c_dcg_var_as_dvector(vector));
    DCG_CHECK_INT(c_dcg_d_vector_size(c_dcg_var_as_dvector(&vector_ref)), 2);
    DCG_CHECK(c_dcg_var_equals(&vector_ref, vector));
    DCG_CHECK(c_dcg_var_is_truthy(&vector_ref));
    DCG_CHECK_INT(c_dcg_var_cast(&out, &vector_ref, VAR_TYPE_D_VECTOR), DCG_OK);
    DCG_CHECK(c_dcg_var_as_dvector(&out) == c_dcg_var_as_dvector(vector));
    DCG_CHECK_INT(c_dcg_var_cast(&out, &vector_ref, VAR_TYPE_D_MATRIX), DCG_ERR_BAD_CAST);
    c_dcg_var_free(vector); /* the reference owned nothing to release */
}

#if DCG_VIGILANT
/* Run one mismatched read in a child process and check it died of the abort
 * rather than handing back a value that reads as real data. */
static void dcg_expect_abort(int probe) {
    (void) fflush(NULL);
    pid_t child = fork();
    if (child == 0) {
        dcg_var_t strings       = dcg_t_var_string("not a number");
        dcg_var_t integers      = dcg_t_var_int(1);
        dcg_var_t dead_double;
        dcg_var_t dead_string;
        dcg_var_t dead_vector;
        dcg_var_t deep_dead;

        (void) c_dcg_var_init_ref_raw(&dead_double, VAR_TYPE_DOUBLE_REF, NULL);
        (void) c_dcg_var_init_ref_raw(&dead_string, VAR_TYPE_STRING_REF, NULL);
        (void) c_dcg_var_init_ref_raw(&dead_vector, VAR_TYPE_D_VECTOR_REF, NULL);
        (void) c_dcg_var_init_ref_raw(&deep_dead, VAR_TYPE_DOUBLE_REF_REF, NULL);

        switch (probe) {
            case 0:
                (void) c_dcg_var_as_double(&strings);
                break;
            case 1:
                (void) c_dcg_var_as_int(&strings);
                break;
            case 2:
                (void) c_dcg_var_as_string(&integers);
                break;
            case 3:
                (void) c_dcg_var_as_dvector(&integers);
                break;
            case 4:
                (void) c_dcg_var_as_ptr(&integers);
                break;
            case 5:
                (void) c_dcg_var_as_ref(&integers);
                break;
            case 6:
                (void) c_dcg_var_as_double(&dead_double); /* a reference pointing at nothing */
                break;
            case 7:
                (void) c_dcg_var_as_dvector(&dead_vector);
                break;
            case 8:
                (void) c_dcg_var_is_null(&dead_string); /* a predicate that must read through it */
                break;
            default:
                (void) c_dcg_var_as_double(&deep_dead); /* a ladder whose middle is missing */
                break;
        }
        _exit(0); /* only reached when the read returned instead of refusing */
    }

    int status = 0;
    DCG_CHECK(child > 0);
    DCG_CHECK_INT(waitpid(child, &status, 0), child);
    DCG_CHECK(WIFSIGNALED(status)); /* died of a signal, not of a clean exit */
    DCG_CHECK_INT(WTERMSIG(status), SIGABRT);
}

static void test_vigilant_abort(void) {
    /* A read with no correct value to return - the wrong type, or a reference
     * pointing at nothing - names itself and the tag on stderr and aborts. Every
     * probe below is one such read. */
    for (int probe = 0; probe < 10; probe++) dcg_expect_abort(probe);
}
#endif  // DCG_VIGILANT

static void test_predicates(void) {
    dcg_var_t integers = dcg_t_var_int(1);
    dcg_var_t doubles  = dcg_t_var_double(1.0);
    dcg_var_t strings  = dcg_t_var_string("1");
    dcg_var_t bools    = dcg_t_var_bool(true);

    DCG_CHECK(c_dcg_var_is_numeric(&integers));
    DCG_CHECK(c_dcg_var_is_numeric(&doubles));
    DCG_CHECK(!c_dcg_var_is_numeric(&strings));
    DCG_CHECK(!c_dcg_var_is_numeric(&bools));
    DCG_CHECK(!c_dcg_var_is_numeric(NULL));

    DCG_CHECK(!c_dcg_var_is_container(&integers));
    DCG_CHECK(!c_dcg_var_is_container(NULL));

    DCG_CHECK(c_dcg_var_is_null(&(dcg_var_t){.dtype = VAR_TYPE_RAW_PTR, .value = {.as_ptr = NULL}}));
    DCG_CHECK(!c_dcg_var_is_null(&integers));
}

static void test_truthiness(void) {
    dcg_var_t true_bool    = dcg_t_var_bool(true);
    dcg_var_t false_bool   = dcg_t_var_bool(false);
    dcg_var_t zero_int     = dcg_t_var_int(0);
    dcg_var_t one_int      = dcg_t_var_int(1);
    dcg_var_t zero_double  = dcg_t_var_double(0.0);
    dcg_var_t one_double   = dcg_t_var_double(0.5);
    dcg_var_t empty_string = dcg_t_var_string("");
    dcg_var_t some_string  = dcg_t_var_string("x");
    dcg_var_t null_string  = dcg_t_var_string(NULL);
    dcg_var_t null_ptr;
    (void) c_dcg_var_init(&null_ptr);

    DCG_CHECK(c_dcg_var_is_truthy(&true_bool));
    DCG_CHECK(!c_dcg_var_is_truthy(&false_bool));
    DCG_CHECK(!c_dcg_var_is_truthy(&zero_int));
    DCG_CHECK(c_dcg_var_is_truthy(&one_int));
    DCG_CHECK(!c_dcg_var_is_truthy(&zero_double));
    DCG_CHECK(c_dcg_var_is_truthy(&one_double));
    DCG_CHECK(!c_dcg_var_is_truthy(&empty_string));
    DCG_CHECK(c_dcg_var_is_truthy(&some_string));
    DCG_CHECK(!c_dcg_var_is_truthy(&null_string));
    DCG_CHECK(!c_dcg_var_is_truthy(&null_ptr));
    DCG_CHECK(!c_dcg_var_is_truthy(NULL));

    /* Containers follow the sequence rule: present and non-empty is truthy. */

    double     empty_data[1]  = {0.0};
    double     filled_data[2] = {1.0, 2.0};
    dcg_var_t* zero_vector    = c_dcg_var_new(NULL);
    dcg_var_t* filled_vector  = c_dcg_var_new(NULL);

    (void) c_dcg_var_init_dvector(zero_vector, empty_data, 0, false, NULL);
    DCG_CHECK(!c_dcg_var_is_truthy(zero_vector)); /* an empty sequence is falsy */

    (void) c_dcg_var_init_dvector(filled_vector, filled_data, 2, false, NULL);
    DCG_CHECK(c_dcg_var_is_truthy(filled_vector));

    c_dcg_var_free(zero_vector);
    c_dcg_var_free(filled_vector);
}

static void test_equality(void) {
    dcg_var_t a = dcg_t_var_string("abc");
    dcg_var_t b = dcg_t_var_string("abc");
    dcg_var_t c = dcg_t_var_string("abd");
    dcg_var_t d = dcg_t_var_int(3);
    dcg_var_t e = dcg_t_var_int(3);
    dcg_var_t f = dcg_t_var_double(3.0);

    DCG_CHECK(c_dcg_var_equals(&a, &b)); /* content, not address */
    DCG_CHECK(!c_dcg_var_equals(&a, &c));
    DCG_CHECK(c_dcg_var_equals(&d, &e));
    DCG_CHECK(!c_dcg_var_equals(&d, &f)); /* tag is part of identity */
    DCG_CHECK(c_dcg_var_equals(NULL, NULL));
    DCG_CHECK(!c_dcg_var_equals(&a, NULL));

    /* Containers compare element-wise, shape first. The container struct is
     * allocated under the var, so these have to be heap vars. */
    double     first[2]  = {0.0, 5.0};
    double     second[2] = {0.0, 5.0};
    double     third[3]  = {0.0, 5.0, 0.0};
    dcg_var_t* v1        = c_dcg_var_new(NULL);
    dcg_var_t* v2        = c_dcg_var_new(NULL);
    dcg_var_t* v3        = c_dcg_var_new(NULL);

    (void) c_dcg_var_init_dvector(v1, first, 2, false, NULL);
    (void) c_dcg_var_init_dvector(v2, second, 2, false, NULL);
    (void) c_dcg_var_init_dvector(v3, third, 3, false, NULL);

    DCG_CHECK(c_dcg_var_equals(v1, v2));
    DCG_CHECK(!c_dcg_var_equals(v1, v3)); /* different shape */

    second[1] = 6.0;
    DCG_CHECK(!c_dcg_var_equals(v1, v2)); /* same shape, different content */

    /* Matrices compare shape, layout and content. */
    double     matrix_data[4] = {1.0, 2.0, 3.0, 4.0};
    dcg_var_t* row_major      = c_dcg_var_new(NULL);
    dcg_var_t* column_major   = c_dcg_var_new(NULL);
    (void) c_dcg_var_init_dmatrix(row_major, matrix_data, 2, 2, true, false, NULL);
    (void) c_dcg_var_init_dmatrix(column_major, matrix_data, 2, 2, false, false, NULL);
    DCG_CHECK(!c_dcg_var_equals(row_major, column_major)); /* the layout is part of the value */
    (void) c_dcg_var_init_dmatrix(column_major, matrix_data, 2, 2, true, false, NULL);
    DCG_CHECK(c_dcg_var_equals(row_major, column_major));

    c_dcg_var_free(v1);
    c_dcg_var_free(v2);
    c_dcg_var_free(v3);
    c_dcg_var_free(row_major);
    c_dcg_var_free(column_major);
}

static void test_coercion(void) {
    dcg_var_t integer = dcg_t_var_int(3);
    dcg_var_t doubles = dcg_t_var_double(2.75);
    dcg_var_t strings = dcg_t_var_string("nope");
    dcg_var_t out;

    DCG_CHECK(c_dcg_var_as_double(&integer) == 3.0);
    DCG_CHECK(c_dcg_var_as_int(&doubles) == 2); /* truncates toward zero */
    DCG_CHECK_INT(c_dcg_var_as_bool(&integer), true);
    DCG_CHECK(c_dcg_var_as_double(NULL) == 0.0);
    DCG_CHECK_INT(c_dcg_var_as_int(NULL), 0);

    DCG_CHECK_INT(c_dcg_var_cast(&out, &integer, VAR_TYPE_DOUBLE), DCG_OK);
    DCG_CHECK(out.value.as_double == 3.0);

    DCG_CHECK_INT(c_dcg_var_cast(&out, &doubles, VAR_TYPE_BOOL), DCG_OK);
    DCG_CHECK(out.value.as_bool);

    DCG_CHECK_INT(c_dcg_var_cast(&out, &integer, VAR_TYPE_INT), DCG_OK);
    DCG_CHECK_INT(out.value.as_int, 3);

    /* Strings and containers are never silently reinterpreted. */
    DCG_CHECK_INT(c_dcg_var_cast(&out, &strings, VAR_TYPE_INT), DCG_ERR_BAD_CAST);
    DCG_CHECK_INT(c_dcg_var_cast(&out, &strings, VAR_TYPE_DOUBLE), DCG_ERR_BAD_CAST);
    DCG_CHECK_INT(c_dcg_var_cast(&out, NULL, VAR_TYPE_INT), DCG_ERR_INVALID_ARG);

    dcg_var_t* vector_value = c_dcg_var_new_dvector(2, NULL);
    DCG_CHECK_INT(c_dcg_var_cast(&out, vector_value, VAR_TYPE_DOUBLE), DCG_ERR_BAD_CAST);
    DCG_CHECK_INT(c_dcg_var_cast(&out, vector_value, VAR_TYPE_INT), DCG_ERR_BAD_CAST);
    DCG_CHECK_INT(c_dcg_var_cast(&out, vector_value, VAR_TYPE_D_VECTOR), DCG_OK);
    DCG_CHECK(c_dcg_var_as_dvector(&out) != NULL);
    DCG_CHECK_INT(c_dcg_var_cast(&out, vector_value, VAR_TYPE_BOOL), DCG_OK);
    DCG_CHECK(c_dcg_var_as_bool(&out)); /* non-empty container is truthy */
    c_dcg_var_free(vector_value);

    /* A NULL out-parameter only asks "would this succeed?". */
    DCG_CHECK_INT(c_dcg_var_cast(NULL, &integer, VAR_TYPE_DOUBLE), DCG_OK);
}

static void test_formatting(void) {
    char      buf[64];

    dcg_var_t bools       = dcg_t_var_bool(true);
    dcg_var_t doubles     = dcg_t_var_double(1.5);
    dcg_var_t integers    = dcg_t_var_int(-7);
    dcg_var_t strings     = dcg_t_var_string("hi");
    dcg_var_t null_string = dcg_t_var_string(NULL);

    DCG_CHECK(c_dcg_var_format(&bools, buf, sizeof(buf)) > 0);
    DCG_CHECK_STR(buf, "true");

    DCG_CHECK(c_dcg_var_format(&doubles, buf, sizeof(buf)) > 0);
    DCG_CHECK_STR(buf, "1.5");

    DCG_CHECK(c_dcg_var_format(&integers, buf, sizeof(buf)) > 0);
    DCG_CHECK_STR(buf, "-7");

    DCG_CHECK(c_dcg_var_format(&strings, buf, sizeof(buf)) > 0);
    DCG_CHECK_STR(buf, "\"hi\"");

    DCG_CHECK(c_dcg_var_format(&null_string, buf, sizeof(buf)) > 0);
    DCG_CHECK_STR(buf, "NULL");

    DCG_CHECK(c_dcg_var_format(NULL, buf, sizeof(buf)) > 0);
    DCG_CHECK_STR(buf, "(null)");

    /* Containers render their shape, not their contents. */
    dcg_var_t* vector_value = c_dcg_var_new_dvector(3, NULL);
    DCG_CHECK(c_dcg_var_format(vector_value, buf, sizeof(buf)) > 0);
    DCG_CHECK_STR(buf, "d_vector(n=3)");
    c_dcg_var_free(vector_value);

    dcg_var_t* matrix_value = c_dcg_var_new_dmatrix(2, 3, true, NULL);
    DCG_CHECK(c_dcg_var_format(matrix_value, buf, sizeof(buf)) > 0);
    DCG_CHECK_STR(buf, "d_matrix(2x3)");
    c_dcg_var_free(matrix_value);

    DCG_CHECK_INT(c_dcg_var_format(&integers, NULL, 0), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_var_format(&integers, buf, 0), DCG_ERR_INVALID_ARG);
}

static void test_snapshot_semantics(void) {
    /* copy = false wraps: the value reads the caller's buffer directly. */
    double     source[3] = {1.0, 2.0, 3.0};
    dcg_var_t* wrapped   = c_dcg_var_new(NULL);
    DCG_CHECK_INT(c_dcg_var_init_dvector(wrapped, source, 3, false, NULL), DCG_OK);
    DCG_CHECK(c_dcg_var_as_dvector(wrapped)->data == source);
    source[0] = 99.0;
    DCG_CHECK(c_dcg_d_vector_at(c_dcg_var_as_dvector(wrapped), 0) == 99.0);
    c_dcg_var_free(wrapped); /* frees the shape struct only - the source is the caller's */

    /* copy = true snapshots: later writes to the source cannot reach the value. */
    double     original[3] = {1.0, 2.0, 3.0};
    dcg_var_t* snapshot    = c_dcg_var_new(NULL);
    DCG_CHECK_INT(c_dcg_var_init_dvector(snapshot, original, 3, true, NULL), DCG_OK);
    DCG_CHECK(c_dcg_var_as_dvector(snapshot)->data != original);
    DCG_CHECK(c_dcg_d_vector_at(c_dcg_var_as_dvector(snapshot), 0) == 1.0);

    original[0] = 99.0;
    DCG_CHECK(c_dcg_d_vector_at(c_dcg_var_as_dvector(snapshot), 0) == 1.0); /* immune */
    c_dcg_var_free(snapshot);                                               /* frees the snapshot, not the source */

    /* The matrix flavour snapshots its flat buffer the same way. */
    double     flat[4] = {1.0, 2.0, 3.0, 4.0};
    dcg_var_t* matrix  = c_dcg_var_new(NULL);
    DCG_CHECK_INT(c_dcg_var_init_dmatrix(matrix, flat, 2, 2, true, true, NULL), DCG_OK);
    DCG_CHECK(c_dcg_var_as_dmatrix(matrix)->data != flat);
    flat[0] = 99.0;
    DCG_CHECK(c_dcg_d_matrix_at(c_dcg_var_as_dmatrix(matrix), 0, 0) == 1.0);
    c_dcg_var_free(matrix);

    /* Nothing to snapshot: an empty source stays an empty value. */
    dcg_var_t* empty = c_dcg_var_new(NULL);
    DCG_CHECK_INT(c_dcg_var_init_dvector(empty, NULL, 0, true, NULL), DCG_OK);
    DCG_CHECK(c_dcg_var_is_null(empty));
    c_dcg_var_free(empty);
}

static void test_owned_payload_chain(void) {
    /* One allocation chain, one free: the value owns its string, its vector
     * and its matrix, and LSan proves nothing is left behind. */
    dcg_var_t* string_value = c_dcg_var_new_string("chain", NULL);
    dcg_var_t* vector_value = c_dcg_var_new_dvector(8, NULL);
    dcg_var_t* matrix_value = c_dcg_var_new_dmatrix(4, 4, true, NULL);

    DCG_CHECK(string_value && vector_value && matrix_value);

    c_dcg_var_free(string_value);
    c_dcg_var_free(vector_value);
    c_dcg_var_free(matrix_value);
}

int main(void) {
    (void) printf("test_c_var\n");
    DCG_RUN(test_init_population);
    DCG_RUN(test_owning_constructors);
    DCG_RUN(test_owned_string);
    DCG_RUN(test_vectors);
    DCG_RUN(test_matrices);
    DCG_RUN(test_container_values);
    DCG_RUN(test_type_names);
    DCG_RUN(test_reference_tags);
    DCG_RUN(test_reference_raw);
    DCG_RUN(test_reference_to_value);
    DCG_RUN(test_reference_level_one);
    DCG_RUN(test_reference_consumers);
#if DCG_VIGILANT
    DCG_RUN(test_vigilant_abort);
#endif
    DCG_RUN(test_predicates);
    DCG_RUN(test_truthiness);
    DCG_RUN(test_equality);
    DCG_RUN(test_coercion);
    DCG_RUN(test_formatting);
    DCG_RUN(test_snapshot_semantics);
    DCG_RUN(test_owned_payload_chain);
    DCG_SUMMARY("test_c_var");
    return dcg_test_failures == 0 ? 0 : 1;
}
