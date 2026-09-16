/*
 * c_edge.h - edge conditions: the static built-ins, pointer identity,
 * branch matching, and the standalone-condition lifecycle.
 */

#include <decision_graph/decision_tree/bake/c_edge.h>

#include "test_util.h"

/*
 * Runs FIRST, before anything else touches a built-in: the whole point of
 * the guard is that the storage is usable before the one-time init has run.
 */
static void test_lazy_init(void) {
    DCG_CHECK(!__DCG_CONDITION_INITIALIZED);
    DCG_CHECK_INT(__DCG_NO_CONDITION.repr[0], '\0'); /* not written yet */
    DCG_CHECK_INT(__DCG_TRUE_CONDITION.repr[0], '\0');

    /* The identity predicates compare storage addresses, so they answer
     * correctly while the handles are still unpublished. */
    DCG_CHECK(c_dcg_condition_is_none(NULL));
    DCG_CHECK(!c_dcg_condition_is_else(NULL));
    DCG_CHECK(!c_dcg_condition_is_true(NULL));
    DCG_CHECK(!c_dcg_condition_is_sentinel(NULL));
    DCG_CHECK(!__DCG_CONDITION_INITIALIZED); /* ...and they do not trigger the init */

    /* The first use of a macro writes the built-ins, once. */
    const dcg_node_edge_condition* none = DCG_NO_CONDITION;
    DCG_CHECK(__DCG_CONDITION_INITIALIZED);
    DCG_CHECK(none == &__DCG_NO_CONDITION);
    DCG_CHECK(none == &__DCG_NO_CONDITION);
    DCG_CHECK(none->repr[0] != '\0');

    /* The flag is the guard: a second call is a no-op, and the handles the
     * graph already holds keep pointing at the same objects. */
    c_dcg_condition_init_globals();
    DCG_CHECK(none == &__DCG_NO_CONDITION);
    DCG_CHECK(DCG_TRUE_CONDITION == &__DCG_TRUE_CONDITION);
}

static void test_builtins_are_static(void) {
    /* Each macro publishes a const handle onto static storage: stable,
     * comparable, never allocated - nothing to free and nothing to leak. */
    DCG_CHECK(DCG_NO_CONDITION == &__DCG_NO_CONDITION);
    DCG_CHECK(DCG_NO_CONDITION == DCG_NO_CONDITION);
    DCG_CHECK(DCG_ELSE_CONDITION == &__DCG_ELSE_CONDITION);
    DCG_CHECK(DCG_AUTO_CONDITION == &__DCG_AUTO_CONDITION);
    DCG_CHECK(DCG_TRUE_CONDITION == &__DCG_TRUE_CONDITION);
    DCG_CHECK(DCG_FALSE_CONDITION == &__DCG_FALSE_CONDITION);

    DCG_CHECK(DCG_NO_CONDITION == DCG_NO_CONDITION);
    DCG_CHECK(DCG_TRUE_CONDITION != DCG_FALSE_CONDITION);
    DCG_CHECK(DCG_ELSE_CONDITION != DCG_NO_CONDITION);

    /* The repr names the condition and its OWN address, which is what makes
     * it run-time work: the address can only be known once the object is. */
    char expect[DCG_EDGE_REPR_MAXLEN];

    (void) snprintf(expect, sizeof(expect), "<CONDITION 0x%zx>(Unconditional)", (size_t) (uintptr_t) DCG_NO_CONDITION);
    DCG_CHECK_STR(DCG_NO_CONDITION->repr, expect);

    (void) snprintf(expect, sizeof(expect), "<CONDITION Internal 0x%zx>(Else)", (size_t) (uintptr_t) DCG_ELSE_CONDITION);
    DCG_CHECK_STR(DCG_ELSE_CONDITION->repr, expect);

    (void) snprintf(expect, sizeof(expect), "<CONDITION Internal 0x%zx>(Auto)", (size_t) (uintptr_t) DCG_AUTO_CONDITION);
    DCG_CHECK_STR(DCG_AUTO_CONDITION->repr, expect);

    (void) snprintf(expect, sizeof(expect), "<CONDITION 0x%zx>(True)", (size_t) (uintptr_t) DCG_TRUE_CONDITION);
    DCG_CHECK_STR(DCG_TRUE_CONDITION->repr, expect);

    (void) snprintf(expect, sizeof(expect), "<CONDITION 0x%zx>(False)", (size_t) (uintptr_t) DCG_FALSE_CONDITION);
    DCG_CHECK_STR(DCG_FALSE_CONDITION->repr, expect);

    /* Every built-in is a distinct object. */
    DCG_CHECK(DCG_NO_CONDITION != DCG_ELSE_CONDITION);
    DCG_CHECK(DCG_ELSE_CONDITION != DCG_AUTO_CONDITION);
    DCG_CHECK(DCG_AUTO_CONDITION != DCG_TRUE_CONDITION);

    /* Only the boolean branches carry a payload. */
    DCG_CHECK(c_dcg_var_as_bool(&DCG_TRUE_CONDITION->value));
    DCG_CHECK(!c_dcg_var_as_bool(&DCG_FALSE_CONDITION->value));
    (void) printf("    %-26s repr=\"%s\"\n", "TRUE_CONDITION", DCG_TRUE_CONDITION->repr);
    (void) printf("    %-26s repr=\"%s\"\n", "NO_CONDITION", DCG_NO_CONDITION->repr);
    DCG_CHECK_INT(DCG_TRUE_CONDITION->value.dtype, VAR_TYPE_BOOL);
    DCG_CHECK_INT(DCG_NO_CONDITION->value.dtype, VAR_TYPE_RAW_PTR);
}

static void test_identity_predicates(void) {
    DCG_CHECK(c_dcg_condition_is_none(DCG_NO_CONDITION));
    DCG_CHECK(c_dcg_condition_is_none(NULL)); /* a NULL edge is the unconditional one */
    DCG_CHECK(!c_dcg_condition_is_none(DCG_ELSE_CONDITION));

    DCG_CHECK(c_dcg_condition_is_else(DCG_ELSE_CONDITION));
    DCG_CHECK(c_dcg_condition_is_auto(DCG_AUTO_CONDITION));
    DCG_CHECK(c_dcg_condition_is_true(DCG_TRUE_CONDITION));
    DCG_CHECK(c_dcg_condition_is_false(DCG_FALSE_CONDITION));

    DCG_CHECK(c_dcg_condition_is_sentinel(DCG_NO_CONDITION));
    DCG_CHECK(c_dcg_condition_is_sentinel(DCG_ELSE_CONDITION));
    DCG_CHECK(c_dcg_condition_is_sentinel(DCG_AUTO_CONDITION));
    DCG_CHECK(c_dcg_condition_is_sentinel(DCG_TRUE_CONDITION));
    DCG_CHECK(c_dcg_condition_is_sentinel(DCG_FALSE_CONDITION));
    DCG_CHECK(!c_dcg_condition_is_sentinel(NULL));

    DCG_CHECK(c_dcg_condition_is_binary(DCG_TRUE_CONDITION));
    DCG_CHECK(c_dcg_condition_is_binary(DCG_FALSE_CONDITION));
    DCG_CHECK(!c_dcg_condition_is_binary(DCG_ELSE_CONDITION));
    DCG_CHECK(!c_dcg_condition_is_binary(DCG_NO_CONDITION));
    DCG_CHECK(!c_dcg_condition_is_binary(NULL));

    /* The predicates are pure identity: a user condition is never a built-in. */
    dcg_node_edge_condition own;
    c_dcg_condition_init(&own, dcg_t_var_bool(true), "own");
    DCG_CHECK(!c_dcg_condition_is_sentinel(&own));
    DCG_CHECK(!c_dcg_condition_is_true(&own));
    DCG_CHECK(!c_dcg_condition_is_none(&own));
}

static void test_condition_equality(void) {
    dcg_node_edge_condition a;
    dcg_node_edge_condition b;
    dcg_node_edge_condition c;

    c_dcg_condition_init(&a, dcg_t_var_int(5), "five");
    c_dcg_condition_init(&b, dcg_t_var_int(5), "also five");
    c_dcg_condition_init(&c, dcg_t_var_int(6), "six");

    /* Built-ins compare by identity... */
    DCG_CHECK(c_dcg_condition_equals(DCG_TRUE_CONDITION, DCG_TRUE_CONDITION));
    DCG_CHECK(!c_dcg_condition_equals(DCG_TRUE_CONDITION, DCG_FALSE_CONDITION));
    /* ...and a NULL edge is the unconditional one. */
    DCG_CHECK(c_dcg_condition_equals(NULL, DCG_NO_CONDITION));
    DCG_CHECK(!c_dcg_condition_equals(NULL, DCG_TRUE_CONDITION));

    /* User conditions compare by payload: a repr difference is not an edge
     * difference, but a value difference is. */
    DCG_CHECK(c_dcg_condition_equals(&a, &b));
    DCG_CHECK(!c_dcg_condition_equals(&a, &c));

    /* A user condition never equals a built-in, whatever it carries. */
    dcg_node_edge_condition true_like;
    c_dcg_condition_init(&true_like, dcg_t_var_bool(true), "true-like");
    DCG_CHECK(!c_dcg_condition_equals(&true_like, DCG_TRUE_CONDITION));
}

static void test_branch_matching(void) {
    dcg_var_t true_value  = dcg_t_var_bool(true);
    dcg_var_t false_value = dcg_t_var_bool(false);

    /* Unconditional / else / auto always select; the caller orders them. */
    DCG_CHECK(c_dcg_condition_matches(DCG_NO_CONDITION, &true_value));
    DCG_CHECK(c_dcg_condition_matches(DCG_NO_CONDITION, &false_value));
    DCG_CHECK(c_dcg_condition_matches(DCG_ELSE_CONDITION, &false_value));
    DCG_CHECK(c_dcg_condition_matches(DCG_AUTO_CONDITION, &false_value));
    DCG_CHECK(c_dcg_condition_matches(NULL, &false_value));

    DCG_CHECK(c_dcg_condition_matches(DCG_TRUE_CONDITION, &true_value));
    DCG_CHECK(!c_dcg_condition_matches(DCG_TRUE_CONDITION, &false_value));
    DCG_CHECK(c_dcg_condition_matches(DCG_FALSE_CONDITION, &false_value));
    DCG_CHECK(!c_dcg_condition_matches(DCG_FALSE_CONDITION, &true_value));

    /* A user condition compares the parent's value against its payload. */
    dcg_node_edge_condition seven;
    c_dcg_condition_init(&seven, dcg_t_var_int(7), "seven");

    dcg_var_t seven_value = dcg_t_var_int(7);
    dcg_var_t eight_value = dcg_t_var_int(8);
    DCG_CHECK(c_dcg_condition_matches(&seven, &seven_value));
    DCG_CHECK(!c_dcg_condition_matches(&seven, &eight_value));

    /* Truthiness drives the boolean branches, so 0 / "" / NULL take FALSE. */
    dcg_var_t zero  = dcg_t_var_int(0);
    dcg_var_t empty = dcg_t_var_string("");
    DCG_CHECK(c_dcg_condition_matches(DCG_FALSE_CONDITION, &zero));
    DCG_CHECK(c_dcg_condition_matches(DCG_FALSE_CONDITION, &empty));
    DCG_CHECK(!c_dcg_condition_matches(DCG_TRUE_CONDITION, &empty));
}

static void test_init_and_lifecycle(void) {
    dcg_node_edge_condition condition;

    /* The repr is copied into the inline buffer, not referenced. */
    DCG_CHECK_INT(c_dcg_condition_init(&condition, dcg_t_var_string("key"), "key"), DCG_OK);
    DCG_CHECK_STR(condition.repr, "key");
    DCG_CHECK_STR(c_dcg_var_as_string(&condition.value), "key");
    DCG_CHECK_STR(c_dcg_condition_repr(&condition), "key");

    /* A long repr is truncated and stays NUL-terminated. */
    char long_repr[DCG_EDGE_REPR_MAXLEN + 64];
    for (size_t i = 0; i < sizeof(long_repr) - 1; i++) long_repr[i] = 'x';
    long_repr[sizeof(long_repr) - 1] = '\0';

    DCG_CHECK_INT(c_dcg_condition_init(&condition, dcg_t_var_int(1), long_repr), DCG_OK);
    DCG_CHECK_INT(strlen(condition.repr), DCG_EDGE_REPR_MAXLEN - 1);

    /* No repr: the buffer starts empty and formatting falls back to the value. */
    DCG_CHECK_INT(c_dcg_condition_init(&condition, dcg_t_var_int(42), NULL), DCG_OK);
    DCG_CHECK_INT(condition.repr[0], '\0');

    /* dealloc zeroes the buf but never frees it - embedded use depends on it. */
    c_dcg_condition_dealloc(&condition);
    DCG_CHECK_INT(condition.repr[0], '\0');
    DCG_CHECK(c_dcg_var_is_null(&condition.value));

    /* Every entry point is NULL-safe. */
    c_dcg_condition_dealloc(NULL);
    c_dcg_condition_free(NULL);
    DCG_CHECK_INT(c_dcg_condition_init(NULL, dcg_t_var_int(1), "x"), DCG_ERR_INVALID_ARG);
    DCG_CHECK_STR(c_dcg_condition_repr(NULL), "");
}

static void test_standalone_condition(void) {
    /* What a builder uses for the conditions that are not built-ins. */
    dcg_node_edge_condition* condition = c_dcg_edge_new(dcg_t_var_string("key-a"), "key-a", NULL);
    DCG_CHECK(condition != NULL);
    DCG_CHECK(!c_dcg_condition_is_sentinel(condition));
    DCG_CHECK_STR(condition->repr, "key-a");
    DCG_CHECK_STR(c_dcg_var_as_string(&condition->value), "key-a");
    c_dcg_condition_free(condition);

    /* A condition owns nothing, so freeing one frees exactly one block. */
    dcg_node_edge_condition* bare = c_dcg_edge_new(dcg_t_var_int(0), NULL, NULL);
    DCG_CHECK(bare != NULL);
    DCG_CHECK_INT(bare->repr[0], '\0');
    c_dcg_condition_free(bare);
}

static void test_formatting(void) {
    char buf[DCG_EDGE_REPR_MAXLEN];

    /* The address in the repr is the object's own, so assert on the shape
     * that is stable across runs. */
    DCG_CHECK(c_dcg_condition_format(DCG_TRUE_CONDITION, "", buf, sizeof(buf)) > 0);
    DCG_CHECK_CONTAINS(buf, "<CONDITION 0x");
    DCG_CHECK_CONTAINS(buf, "(True)");

    DCG_CHECK(c_dcg_condition_format(DCG_NO_CONDITION, "", buf, sizeof(buf)) > 0);
    DCG_CHECK_CONTAINS(buf, "<CONDITION 0x");
    DCG_CHECK_CONTAINS(buf, "(Unconditional)");

    DCG_CHECK(c_dcg_condition_format(NULL, "", buf, sizeof(buf)) > 0);
    DCG_CHECK_STR(buf, "NO_CONDITION");

    /* A repr-less condition renders its payload, with the fallback prefix. */
    dcg_node_edge_condition value_only;
    c_dcg_condition_init(&value_only, dcg_t_var_int(42), NULL);
    DCG_CHECK(c_dcg_condition_format(&value_only, "", buf, sizeof(buf)) > 0);
    DCG_CHECK_STR(buf, "42");
    DCG_CHECK(c_dcg_condition_format(&value_only, "key=", buf, sizeof(buf)) > 0);
    DCG_CHECK_STR(buf, "key=42");

    DCG_CHECK_INT(c_dcg_condition_format(DCG_TRUE_CONDITION, "", NULL, 0), DCG_ERR_INVALID_ARG);
}

int main(void) {
    (void) printf("test_c_edge\n");
    DCG_RUN(test_lazy_init); /* must stay first: it asserts the pre-init state */
    DCG_RUN(test_builtins_are_static);
    DCG_RUN(test_identity_predicates);
    DCG_RUN(test_condition_equality);
    DCG_RUN(test_branch_matching);
    DCG_RUN(test_init_and_lifecycle);
    DCG_RUN(test_standalone_condition);
    DCG_RUN(test_formatting);
    DCG_SUMMARY("test_c_edge");
    return dcg_test_failures == 0 ? 0 : 1;
}
