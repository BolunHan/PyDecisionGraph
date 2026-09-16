#ifndef C_DCG_TEST_UTIL_H
#define C_DCG_TEST_UTIL_H

/*
 * Minimal assertion harness for the bake C suites.
 *
 * Every suite is a standalone binary: it includes this header, records its
 * own pass/fail counters, and exits non-zero when anything failed. The
 * counters are file-scope statics, which is exactly right here - one
 * translation unit per suite, one process per suite.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int dcg_test_checks   = 0;
static int dcg_test_failures = 0;

/** Assert a condition, reporting file:line with the expression on failure. */
#define DCG_CHECK(cond)                                                              \
    do {                                                                             \
        dcg_test_checks++;                                                           \
        if (!(cond)) {                                                               \
            dcg_test_failures++;                                                     \
            (void) fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                            \
    } while (0)

/** Assert two integers are equal, printing both values on failure. */
#define DCG_CHECK_INT(actual, expected)                                                                                                          \
    do {                                                                                                                                         \
        dcg_test_checks++;                                                                                                                       \
        long long dcg_actual   = (long long) (actual);                                                                                           \
        long long dcg_expected = (long long) (expected);                                                                                         \
        if (dcg_actual != dcg_expected) {                                                                                                        \
            dcg_test_failures++;                                                                                                                 \
            (void) fprintf(stderr, "  FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #actual, #expected, dcg_actual, dcg_expected); \
        }                                                                                                                                        \
    } while (0)

/** Assert two strings are equal (NULL-safe), printing both on failure. */
#define DCG_CHECK_STR(actual, expected)                                                                                                                                        \
    do {                                                                                                                                                                       \
        dcg_test_checks++;                                                                                                                                                     \
        const char* dcg_a    = (actual);                                                                                                                                       \
        const char* dcg_e    = (expected);                                                                                                                                     \
        bool        dcg_same = (dcg_a == dcg_e) || (dcg_a && dcg_e && strcmp(dcg_a, dcg_e) == 0);                                                                              \
        if (!dcg_same) {                                                                                                                                                       \
            dcg_test_failures++;                                                                                                                                               \
            (void) fprintf(stderr, "  FAIL %s:%d: %s == %s (\"%s\" != \"%s\")\n", __FILE__, __LINE__, #actual, #expected, dcg_a ? dcg_a : "(null)", dcg_e ? dcg_e : "(null)"); \
        }                                                                                                                                                                      \
    } while (0)

/** Assert a string contains a substring (NULL-safe). */
#define DCG_CHECK_CONTAINS(haystack, needle)                                                                                                     \
    do {                                                                                                                                         \
        dcg_test_checks++;                                                                                                                       \
        const char* dcg_h = (haystack);                                                                                                          \
        const char* dcg_n = (needle);                                                                                                            \
        if (!dcg_h || !dcg_n || !strstr(dcg_h, dcg_n)) {                                                                                         \
            dcg_test_failures++;                                                                                                                 \
            (void) fprintf(stderr, "  FAIL %s:%d: %s contains %s (\"%s\")\n", __FILE__, __LINE__, #haystack, #needle, dcg_h ? dcg_h : "(null)"); \
        }                                                                                                                                        \
    } while (0)

/** Run a test function, printing its name as it goes. */
#define DCG_RUN(fn)                                                               \
    do {                                                                          \
        int dcg_before = dcg_test_failures;                                       \
        (void) printf("  %-34s ", #fn);                                           \
        (void) fflush(stdout);                                                    \
        fn();                                                                     \
        (void) printf("%s\n", dcg_test_failures == dcg_before ? "ok" : "FAILED"); \
    } while (0)

/** Print the suite summary; expand to a `return` for main(). */
#define DCG_SUMMARY(name)                          \
    (void) printf(                                 \
        "%s: %d checks, %d failures\n",            \
        (name), dcg_test_checks, dcg_test_failures \
    )

#if defined(C_DCG_BAKE_VAR_H)

/*
 * Value builders for suite readability. The real API populates through a
 * pointer, which is the right thing for graph code but noisy in an assertion;
 * these wrap it so a test can still write `dcg_t_var_int(7)`. They borrow
 * nothing and own nothing - a test that needs an owning value uses
 * c_dcg_var_new_*() directly.
 */
static inline dcg_var_t dcg_t_var_bool(bool v) {
    dcg_var_t var;
    (void) c_dcg_var_init_bool(&var, v);
    return var;
}

static inline dcg_var_t dcg_t_var_int(ssize_t v) {
    dcg_var_t var;
    (void) c_dcg_var_init_int(&var, v);
    return var;
}

static inline dcg_var_t dcg_t_var_double(double v) {
    dcg_var_t var;
    (void) c_dcg_var_init_double(&var, v);
    return var;
}

static inline dcg_var_t dcg_t_var_offset(ssize_t v) {
    dcg_var_t var;
    (void) c_dcg_var_init_offset(&var, v);
    return var;
}

static inline dcg_var_t dcg_t_var_string(const char* v) {
    dcg_var_t var;
    (void) c_dcg_var_init_string(&var, v);
    return var;
}

#endif  // C_DCG_BAKE_VAR_H

/** Trace one value: its tag and its payload, as a reader would want it. */
static inline void dcg_t_trace_var(const char* what, const dcg_var_t* var) {
    char buf[DCG_VAR_STRING_MAXLEN];
    (void) c_dcg_var_format(var, buf, sizeof(buf));
    (void) printf("    %-26s %-16s %s\n", what ? what : "", var ? c_dcg_var_type_name(var->dtype) : "(null)", buf);
}

#if defined(C_DCG_BAKE_NODE_H)

/*
 * Node fixtures. Each family header has its own constructors, which is right
 * for a builder but noisy inside a test; these wrap the call a suite makes and
 * return NULL if it fails. They are guarded by the header that provides them,
 * so a suite only sees the fixtures it can actually use.
 */
static inline dcg_node* dcg_t_node(dcg_node_type ntype, const char* repr) {
    return c_dcg_node_new(ntype, repr, NULL);
}

/*
 * Traces. The assertions say whether the code is right; a trace says what it was
 * asked to do - the kind, the repr it composed, the value it holds - so a run log
 * can be read on its own, without opening the suite that produced it.
 */
static inline void dcg_t_trace_node(const char* what, const dcg_node* node) {
    char buf[DCG_VAR_STRING_MAXLEN];
    (void) c_dcg_var_format(node ? &node->out : NULL, buf, sizeof(buf));
    (void) printf("    %-26s %-12s repr=\"%s\"  out=%s\n", what ? what : "", node ? c_dcg_node_type_name(node->ntype) : "(null)", node && node->repr ? node->repr : "", buf);
}

static inline void dcg_t_trace_tree(const char* what, const dcg_node* root) {
    dcg_render_opts opts;
    c_dcg_render_opts_default(&opts);
    (void) printf("    %s\n", what ? what : "tree");
    (void) c_dcg_node_render(root, stdout, &opts);
}

#endif  // C_DCG_BAKE_NODE_H

#if defined(C_DCG_BAKE_HIERARCHY_H)

/* The graph's own kinds: a kind and a repr, and nothing to release. */
static inline dcg_node* dcg_t_node_root(const char* repr) {
    dcg_root_node* node = c_dcg_node_new_root(NULL);
    if (node && repr && c_dcg_node_set_repr(&node->base, repr) != DCG_OK) {
        c_dcg_node_free(&node->base);
        return NULL;
    }
    return node ? &node->base : NULL;
}

static inline dcg_node* dcg_t_node_breakpoint(const char* repr) {
    dcg_breakpoint_node* node = c_dcg_node_new_breakpoint(NULL);
    if (node && repr && c_dcg_node_set_repr(&node->base, repr) != DCG_OK) {
        c_dcg_node_free(&node->base);
        return NULL;
    }
    return node ? &node->base : NULL;
}


#endif  // C_DCG_BAKE_HIERARCHY_H

#if defined(C_DCG_BAKE_ACTION_H)

/* The action family: a kind, a repr, and the builder's two fields. */

static inline dcg_node* dcg_t_node_placeholder(const char* repr) {
    dcg_action_node* node = c_dcg_node_new_action_placeholder(false, NULL); /* a stand-in, never auto-connected */
    if (node && repr && c_dcg_node_set_repr(&node->base, repr) != DCG_OK) {
        c_dcg_node_free(&node->base);
        return NULL;
    }
    return node ? &node->base : NULL;
}

static inline dcg_node* dcg_t_node_action(dcg_node_type action_type, const char* repr) {
    dcg_action_node* node = c_dcg_node_new_action(action_type, repr, true, 0, NULL, NULL); /* no signal, no payload */
    return node ? &node->base : NULL;
}

#endif  // C_DCG_BAKE_ACTION_H

#if defined(C_DCG_BAKE_CONST_H)

/* Constant fixtures: the constructor takes the value, the fixture retitles. */
static inline dcg_node* dcg_t_const(dcg_node_type ntype, const char* repr) {
    dcg_constant_node* node = c_dcg_node_new_const(ntype, repr, NULL);
    return node ? &node->base : NULL;
}

static inline dcg_node* dcg_t_node_double(const char* repr, double value) {
    dcg_constant_node* node = c_dcg_node_new_const_double(value, NULL);
    if (!node) return NULL;
    if (repr && c_dcg_node_set_repr(&node->base, repr) != DCG_OK) {
        c_dcg_node_free_const(node);
        return NULL;
    }
    return &node->base;
}

static inline dcg_node* dcg_t_node_int(const char* repr, ssize_t value) {
    dcg_constant_node* node = c_dcg_node_new_const_int(value, NULL);
    if (!node) return NULL;
    if (repr && c_dcg_node_set_repr(&node->base, repr) != DCG_OK) {
        c_dcg_node_free_const(node);
        return NULL;
    }
    return &node->base;
}

static inline dcg_node* dcg_t_node_string(const char* repr, const char* value) {
    dcg_constant_node* node = c_dcg_node_new_const_string(value, NULL);
    if (!node) return NULL;
    if (repr && c_dcg_node_set_repr(&node->base, repr) != DCG_OK) {
        c_dcg_node_free_const(node);
        return NULL;
    }
    return &node->base;
}

static inline dcg_node* dcg_t_node_bool(const char* repr, bool value) {
    dcg_constant_node* node = c_dcg_node_new_const_bool(value, NULL);
    if (!node) return NULL;
    if (repr && c_dcg_node_set_repr(&node->base, repr) != DCG_OK) {
        c_dcg_node_free_const(node);
        return NULL;
    }
    return &node->base;
}

#endif  // C_DCG_BAKE_CONST_H

#if defined(C_DCG_BAKE_EXPR_H)

/** Trace an expression: its operator, the repr it composed, and each operand. */
static inline void dcg_t_trace_expr(const char* what, const dcg_expression_node* node) {
    if (!node) {
        (void) printf("    %-26s (null)\n", what ? what : "");
        return;
    }

    (void) printf("    %-26s %-9s op=%-9s repr=\"%s\"\n", what ? what : "", c_dcg_node_type_name(node->base.ntype), c_dcg_op_code_name(node->op),
                  node->base.repr ? node->base.repr : "");
    for (size_t i = 0; i < node->n_args; i++) {
        dcg_t_trace_var("arg[]", &node->args[i]);
    }
}

/*
 * Expression fixtures: built over constant inputs, which the binding folds in,
 * so the expression the fixture returns is self-contained.
 */
static inline dcg_expression_node* dcg_t_expr(size_t n_args, dcg_node_type ntype) {
    return c_dcg_node_new_expr(n_args, ntype, NULL);
}

static inline dcg_node* dcg_t_node_unary(dcg_op_code op, const char* repr) {
    dcg_constant_node*   one  = c_dcg_node_new_const_int(1, NULL);
    dcg_expression_node* node = c_dcg_node_new_expr_unary(op, &one->base, NULL);
    c_dcg_node_free_const(one); /* the operand took the value, not the node */

    if (!node) return NULL;
    if (repr && c_dcg_node_set_repr(&node->base, repr) != DCG_OK) {
        c_dcg_node_free_expr(node);
        return NULL;
    }
    return &node->base;
}

static inline dcg_node* dcg_t_node_call(const char* repr) {
    dcg_constant_node*   one    = c_dcg_node_new_const_int(1, NULL);
    dcg_node*            inputs[1] = {&one->base};
    dcg_expression_node* node   = c_dcg_node_new_expr_call(DCG_OP_NONE, inputs, 1, repr, NULL);
    c_dcg_node_free_const(one); /* the operand took the value, not the node */

    return node ? &node->base : NULL;
}

static inline dcg_node* dcg_t_node_binary(dcg_op_code op, const char* repr) {
    dcg_constant_node*   lhs  = c_dcg_node_new_const_double(1.0, NULL);
    dcg_constant_node*   rhs  = c_dcg_node_new_const_double(2.0, NULL);
    dcg_expression_node* node = c_dcg_node_new_expr_binary(op, &lhs->base, &rhs->base, NULL);
    c_dcg_node_free_const(lhs);
    c_dcg_node_free_const(rhs);

    if (!node) return NULL;
    if (repr && c_dcg_node_set_repr(&node->base, repr) != DCG_OK) {
        c_dcg_node_free_expr(node);
        return NULL;
    }
    return &node->base;
}

#endif  // C_DCG_BAKE_EXPR_H

#if defined(C_DCG_BAKE_COLLECTION_H)

/** Trace a mapping: every key, the slot it holds, and what sits in that slot. */
static inline void dcg_t_trace_mapping(const char* what, const dcg_mapping_node* node) {
    (void) printf("    %-26s mapping  slots=%zu/%zu\n", what ? what : "", node ? node->n_slots : 0, node ? node->capacity : 0);
    if (!node) return;

    for (const bytemap_entry* entry = c_bytemap_first(&node->idx_mapping); entry; entry = c_bytemap_next(entry)) {
        size_t index = (size_t) c_bytemap_entry_value_as_uintptr(entry);
        dcg_t_trace_var(entry->key, index < node->n_slots ? &node->slots[index] : NULL);
    }
}

/* Collection fixtures: a mapping owns its index and slots, a list is base-only. */
static inline dcg_mapping_node* dcg_t_mapping(size_t capacity, const char* repr) {
    dcg_mapping_node* node = c_dcg_node_new_mapping(capacity, NULL);
    if (!node) return NULL;
    if (repr && c_dcg_node_set_repr(&node->base, repr) != DCG_OK) {
        c_dcg_node_free_mapping(node);
        return NULL;
    }
    return node;
}

static inline dcg_node* dcg_t_node_collection(dcg_node_type ntype, const char* repr) {
    if (ntype == DCG_NODE_MAPPING) {
        dcg_mapping_node* map = dcg_t_mapping(0, repr);
        return map ? &map->base : NULL;
    }
    return c_dcg_node_new(ntype, repr, NULL); /* a list is the base node alone */
}

#endif  // C_DCG_BAKE_COLLECTION_H

#endif  // C_DCG_TEST_UTIL_H
