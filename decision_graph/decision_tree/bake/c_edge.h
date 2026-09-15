#ifndef C_DCG_BAKE_EDGE_H
#define C_DCG_BAKE_EDGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cbase/allocator_protocol/c_allocator_protocol.h>

#include <decision_graph/decision_tree/bake/c_var.h>

// ========== Constants ==========

/** Capacity of a condition's inline display buffer. */
#ifndef DCG_EDGE_REPR_MAXLEN
#define DCG_EDGE_REPR_MAXLEN 128
#endif

// ========== Structs ==========

// clang-format off

/**
 * @brief The condition carried by an edge (i.e. by a child, about its parent).
 *
 * Identity is the address: the five built-in conditions are the static
 * objects below, handed out by the DCG_*_CONDITION macros, and every
 * comparison in the bake layer is an address comparison against them
 * (c_dcg_condition_is_else, c_dcg_condition_matches, ...). Any other
 * condition is a caller-owned object whose `value` is what the parent's
 * evaluated value gets compared against.
 *
 * The repr is an inline buffer naming the condition and its address, so a
 * condition owns no memory at all: the built-ins are static objects and a
 * user condition is one flat block.
 */
typedef struct dcg_node_edge_condition {
    dcg_var_t value;                       // Condition payload (bools for the TRUE/FALSE built-ins).
    char      repr[DCG_EDGE_REPR_MAXLEN];  // Display text, always present.
} dcg_node_edge_condition;

// ========== Built-in Conditions ==========

/*
 * The five built-ins live in static storage: no heap, so nothing can leak
 * and there is nothing to free.
 *
 * Static storage is writable by default, which is what lets the repr carry
 * each condition's own address - that address only exists once the object
 * does, so it is written by the one-time c_dcg_condition_init_globals(),
 * guarded by __DCG_CONDITION_INITIALIZED and triggered from the macros below.
 *
 * They are file-scope objects in a header: every translation unit that
 * includes this header gets its own copy and its own one-time init, so
 * identity is only meaningful WITHIN a translation unit. Build and inspect a
 * graph in the same TU (which is what the module and each suite do).
 */

static dcg_node_edge_condition __DCG_NO_CONDITION;
static dcg_node_edge_condition __DCG_ELSE_CONDITION;
static dcg_node_edge_condition __DCG_AUTO_CONDITION;
static dcg_node_edge_condition __DCG_TRUE_CONDITION;
static dcg_node_edge_condition __DCG_FALSE_CONDITION;

static bool                    __DCG_CONDITION_INITIALIZED = false;

// clang-format on

// ========== Forward Declarations ==========

static inline void c_dcg_condition_init_globals(void);

// Identity (address comparison - the way conditions are told apart)
static inline bool c_dcg_condition_is_sentinel(const dcg_node_edge_condition* cond);
static inline bool c_dcg_condition_is_none(const dcg_node_edge_condition* cond);
static inline bool c_dcg_condition_is_else(const dcg_node_edge_condition* cond);
static inline bool c_dcg_condition_is_auto(const dcg_node_edge_condition* cond);
static inline bool c_dcg_condition_is_true(const dcg_node_edge_condition* cond);
static inline bool c_dcg_condition_is_false(const dcg_node_edge_condition* cond);
static inline bool c_dcg_condition_is_binary(const dcg_node_edge_condition* cond);

// Comparison
static inline bool c_dcg_condition_equals(const dcg_node_edge_condition* lhs, const dcg_node_edge_condition* rhs);
static inline bool c_dcg_condition_matches(const dcg_node_edge_condition* cond, const dcg_var_t* value);

// Lifecycle
static inline int                      c_dcg_condition_init(dcg_node_edge_condition* cond, dcg_var_t value, const char* repr);
static inline dcg_node_edge_condition* c_dcg_edge_new(dcg_var_t value, const char* repr, allocator_protocol* allocator);
static inline void                     c_dcg_condition_dealloc(dcg_node_edge_condition* cond);
static inline void                     c_dcg_condition_free(dcg_node_edge_condition* cond);

// Output
static inline const char* c_dcg_condition_repr(const dcg_node_edge_condition* cond);
static inline int         c_dcg_condition_format(const dcg_node_edge_condition* cond, const char* fallback_prefix, char* out, size_t cap);
static inline int         c_dcg_condition_print(const dcg_node_edge_condition* cond, FILE* stream);

// ========== Utility Functions ==========

/**
 * @brief Write the built-in conditions, once per translation unit.
 *
 * Fills the five static objects: their payloads, and the repr naming each one
 * together with its own address. The flag is raised last, so the macros below
 * only ever hand out a fully written condition.
 *
 * Not synchronized: the bake layer initializes its built-ins while a graph is
 * being built, which is single-threaded work. A caller that builds graphs
 * from several threads should touch one of the DCG_*_CONDITION macros once,
 * up front, from the thread that starts up.
 */
static inline void c_dcg_condition_init_globals(void) {
    if (__DCG_CONDITION_INITIALIZED) return;

    dcg_node_edge_condition* no_condition    = &__DCG_NO_CONDITION;
    dcg_node_edge_condition* else_condition  = &__DCG_ELSE_CONDITION;
    dcg_node_edge_condition* auto_condition  = &__DCG_AUTO_CONDITION;
    dcg_node_edge_condition* true_condition  = &__DCG_TRUE_CONDITION;
    dcg_node_edge_condition* false_condition = &__DCG_FALSE_CONDITION;

    /* --------------------------------------------------------
     * NO_CONDITION
     * -------------------------------------------------------- */

    (void) snprintf(
        no_condition->repr,
        sizeof(no_condition->repr),
        "<CONDITION 0x%zx>(Unconditional)",
        (size_t) (uintptr_t) no_condition
    );

    no_condition->value = (dcg_var_t) {0};

    /* --------------------------------------------------------
     * ELSE_CONDITION
     * -------------------------------------------------------- */

    (void) snprintf(
        else_condition->repr,
        sizeof(else_condition->repr),
        "<CONDITION Internal 0x%zx>(Else)",
        (size_t) (uintptr_t) else_condition
    );

    else_condition->value = (dcg_var_t) {0};

    /* --------------------------------------------------------
     * AUTO_CONDITION
     * -------------------------------------------------------- */

    (void) snprintf(
        auto_condition->repr,
        sizeof(auto_condition->repr),
        "<CONDITION Internal 0x%zx>(Auto)",
        (size_t) (uintptr_t) auto_condition
    );

    auto_condition->value = (dcg_var_t) {0};

    /* --------------------------------------------------------
     * TRUE_CONDITION
     * -------------------------------------------------------- */

    (void) snprintf(
        true_condition->repr,
        sizeof(true_condition->repr),
        "<CONDITION 0x%zx>(True)",
        (size_t) (uintptr_t) true_condition
    );

    true_condition->value = (dcg_var_t) {
        .dtype = VAR_TYPE_BOOL,
        .value = {
            .as_bool = true,
        },
    };

    /* --------------------------------------------------------
     * FALSE_CONDITION
     * -------------------------------------------------------- */

    (void) snprintf(
        false_condition->repr,
        sizeof(false_condition->repr),
        "<CONDITION 0x%zx>(False)",
        (size_t) (uintptr_t) false_condition
    );

    false_condition->value = (dcg_var_t) {
        .dtype = VAR_TYPE_BOOL,
        .value = {
            .as_bool = false,
        },
    };

    __DCG_CONDITION_INITIALIZED = true;
}

// ========== Public APIs ==========

/*
 * The identity predicates compare against the built-in objects themselves,
 * so they answer correctly whether or not the one-time init has run yet.
 */

/**
 * @brief Predicate: is this one of the five built-in conditions?
 *
 * @param cond  Condition to inspect (NULL-safe).
 * @return true for the built-ins, false for NULL or a caller-owned condition.
 */
static inline bool c_dcg_condition_is_sentinel(const dcg_node_edge_condition* cond) {
    if (!cond) return false;
    return cond == &__DCG_NO_CONDITION ||
           cond == &__DCG_ELSE_CONDITION ||
           cond == &__DCG_AUTO_CONDITION ||
           cond == &__DCG_TRUE_CONDITION ||
           cond == &__DCG_FALSE_CONDITION;
}

/**
 * @brief Predicate: is the condition the unconditional one?
 *
 * A NULL condition means the same thing, which is what lets a detached node
 * and a root's child both read as "no condition".
 *
 * @param cond  Condition to inspect (NULL-safe).
 * @return true for NULL or DCG_NO_CONDITION.
 */
static inline bool c_dcg_condition_is_none(const dcg_node_edge_condition* cond) {
    return cond == NULL || cond == &__DCG_NO_CONDITION;
}

/**
 * @brief Predicate: is the condition the else fallback?
 *
 * @param cond  Condition to inspect (NULL-safe).
 * @return true for DCG_ELSE_CONDITION.
 */
static inline bool c_dcg_condition_is_else(const dcg_node_edge_condition* cond) {
    return cond == &__DCG_ELSE_CONDITION;
}

/**
 * @brief Predicate: is the condition an unresolved AUTO?
 *
 * @param cond  Condition to inspect (NULL-safe).
 * @return true for DCG_AUTO_CONDITION.
 */
static inline bool c_dcg_condition_is_auto(const dcg_node_edge_condition* cond) {
    return cond == &__DCG_AUTO_CONDITION;
}

/**
 * @brief Predicate: is the condition the true branch?
 *
 * @param cond  Condition to inspect (NULL-safe).
 * @return true for DCG_TRUE_CONDITION.
 */
static inline bool c_dcg_condition_is_true(const dcg_node_edge_condition* cond) {
    return cond == &__DCG_TRUE_CONDITION;
}

/**
 * @brief Predicate: is the condition the false branch?
 *
 * @param cond  Condition to inspect (NULL-safe).
 * @return true for DCG_FALSE_CONDITION.
 */
static inline bool c_dcg_condition_is_false(const dcg_node_edge_condition* cond) {
    return cond == &__DCG_FALSE_CONDITION;
}

/**
 * @brief Predicate: is the condition a two-way branch (true or false)?
 *
 * @param cond  Condition to inspect (NULL-safe).
 * @return true for DCG_TRUE_CONDITION / DCG_FALSE_CONDITION.
 */
static inline bool c_dcg_condition_is_binary(const dcg_node_edge_condition* cond) {
    return cond == &__DCG_TRUE_CONDITION || cond == &__DCG_FALSE_CONDITION;
}

/**
 * @brief Equality of two conditions: identity first, payload for the rest.
 *
 * The built-ins are unique objects, so two of them are equal only when they
 * are the same one. A NULL condition equals DCG_NO_CONDITION. Anything else
 * is a caller-owned condition and compares by payload - which is what an
 * evaluator uses to pick a branch, so two user conditions carrying the same
 * value are the same edge as far as the graph is concerned.
 *
 * @param lhs  Left condition (NULL-safe: NULL is DCG_NO_CONDITION).
 * @param rhs  Right condition (NULL-safe: NULL is DCG_NO_CONDITION).
 * @return true when the conditions identify the same edge.
 */
static inline bool c_dcg_condition_equals(const dcg_node_edge_condition* lhs, const dcg_node_edge_condition* rhs) {
    if (lhs == rhs) return true;
    if (c_dcg_condition_is_none(lhs) && c_dcg_condition_is_none(rhs)) return true;
    if (c_dcg_condition_is_sentinel(lhs) || c_dcg_condition_is_sentinel(rhs)) return false;
    return c_dcg_var_equals(&lhs->value, &rhs->value);
}

/**
 * @brief Does the parent's evaluated value select this edge?
 *
 * The branch-selection predicate of the evaluator:
 *   - NULL / NO_CONDITION / AUTO  always select,
 *   - ELSE                        always selects (the caller tries it last),
 *   - TRUE / FALSE                select on the value's truthiness,
 *   - anything else               selects on value equality.
 *
 * @param cond   Edge condition (NULL-safe: NULL behaves as NO_CONDITION).
 * @param value  The parent's evaluated value (NULL-safe: NULL is falsy).
 * @return true when the edge is taken.
 */
static inline bool c_dcg_condition_matches(const dcg_node_edge_condition* cond, const dcg_var_t* value) {
    if (c_dcg_condition_is_none(cond) || c_dcg_condition_is_else(cond) || c_dcg_condition_is_auto(cond)) return true;
    if (c_dcg_condition_is_true(cond)) return c_dcg_var_is_truthy(value);
    if (c_dcg_condition_is_false(cond)) return !c_dcg_var_is_truthy(value);
    return c_dcg_var_equals(&cond->value, value);
}

/**
 * @brief Initialize a caller-owned condition.
 *
 * The repr is COPIED into the condition's inline buffer (truncated to
 * DCG_EDGE_REPR_MAXLEN - 1 characters when it does not fit), so the caller
 * keeps nothing alive on its behalf.
 *
 * @param cond   Condition to initialize.
 * @param value  Payload the parent's value is compared against.
 * @param repr   Display text (may be NULL - the buffer then starts empty and
 *               formatting falls back to the payload).
 * @return DCG_OK, or DCG_ERR_INVALID_ARG when cond is NULL.
 */
static inline int c_dcg_condition_init(dcg_node_edge_condition* cond, dcg_var_t value, const char* repr) {
    if (!cond) return DCG_ERR_INVALID_ARG;

    memset(cond, 0, sizeof(*cond));
    cond->value = value;

    if (repr) {
        size_t len = strlen(repr);
        if (len >= sizeof(cond->repr)) len = sizeof(cond->repr) - 1;
        memcpy(cond->repr, repr, len);
        cond->repr[len] = '\0';
    }

    return DCG_OK;
}

/**
 * @brief Allocate and initialize a standalone condition.
 *
 * For the conditions a builder has to create at run time - mapping keys and
 * other value conditions. The built-ins never need this: they already exist
 * as static objects.
 *
 * @param value      Payload the parent's value is compared against.
 * @param repr       Display text (may be NULL).
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The condition, or NULL on OOM.
 */
static inline dcg_node_edge_condition* c_dcg_edge_new(dcg_var_t value, const char* repr, allocator_protocol* allocator) {
    dcg_node_edge_condition* cond = (dcg_node_edge_condition*) c_ap_alloc(sizeof(dcg_node_edge_condition), allocator);
    if (!cond) return NULL;

    c_dcg_condition_init(cond, value, repr);
    return cond;
}

/**
 * @brief Tear down a condition - leaves a zeroed buf.
 *
 * A condition owns nothing, so this only zeroes it. The built-ins must not be
 * passed here: they are static objects, not a caller's buffer.
 *
 * @param cond  Condition to tear down (NULL-safe).
 */
static inline void c_dcg_condition_dealloc(dcg_node_edge_condition* cond) {
    if (!cond) return;
    memset(cond, 0, sizeof(*cond));
}

/**
 * @brief Tear down a condition and free its buf.
 *
 * @param cond  Condition to free (NULL-safe). Must be an ap-allocated
 *              standalone condition, NOT one of the built-ins.
 */
static inline void c_dcg_condition_free(dcg_node_edge_condition* cond) {
    if (!cond) return;
    c_dcg_condition_dealloc(cond);
    c_ap_free_owned(cond);  // a condition may carry nested blocks of its own
}

/**
 * @brief The condition's display text.
 *
 * @param cond  Condition to inspect (NULL-safe).
 * @return Its inline repr, or "" for NULL.
 */
static inline const char* c_dcg_condition_repr(const dcg_node_edge_condition* cond) {
    if (!cond) return "";
    return cond->repr;
}

/**
 * @brief Render a condition into a caller-provided buffer.
 *
 * Uses the inline repr; a caller-owned condition created without one falls
 * back to its payload, prefixed by `fallback_prefix` when given.
 *
 * @param cond             Condition to render (NULL-safe).
 * @param fallback_prefix  Text placed before the payload when the repr is
 *                         empty (may be NULL).
 * @param out              Destination buffer.
 * @param cap              Capacity of out.
 * @return Number of characters written (excluding NUL), or DCG_ERR_*.
 */
static inline int c_dcg_condition_format(const dcg_node_edge_condition* cond, const char* fallback_prefix, char* out, size_t cap) {
    if (!out || cap == 0) return DCG_ERR_INVALID_ARG;
    if (!cond) {
        int n = snprintf(out, cap, "NO_CONDITION");
        return n < 0 ? DCG_ERR_FORMAT : n;
    }
    if (cond->repr[0] != '\0') {
        int n = snprintf(out, cap, "%s", cond->repr);
        return n < 0 ? DCG_ERR_FORMAT : n;
    }

    int written = snprintf(out, cap, "%s", fallback_prefix ? fallback_prefix : "");
    if (written < 0) return DCG_ERR_FORMAT;
    if ((size_t) written >= cap) return DCG_ERR_FULL;

    int n = c_dcg_var_format(&cond->value, out + written, cap - (size_t) written);
    if (n < 0) return n;
    return written + n;
}

/**
 * @brief Print a condition to a stream.
 *
 * @param cond    Condition to print (NULL-safe).
 * @param stream  Destination stream.
 * @return Number of characters written, or DCG_ERR_*.
 */
static inline int c_dcg_condition_print(const dcg_node_edge_condition* cond, FILE* stream) {
    if (!stream) return DCG_ERR_INVALID_ARG;
    char buf[DCG_EDGE_REPR_MAXLEN];
    int  n = c_dcg_condition_format(cond, "", buf, sizeof(buf));
    if (n < 0) return n;
    return fprintf(stream, "%s", buf);
}

/**
 * The five built-in edge conditions. Each expands to a const pointer to its
 * static object, initializing the built-ins on first use; the pointer is
 * stable for the lifetime of the translation unit, so it can be compared
 * with ==, stored in a static node table, and never needs freeing.
 */
#define DCG_NO_CONDITION         \
    (__DCG_CONDITION_INITIALIZED \
         ? (&__DCG_NO_CONDITION) \
         : (c_dcg_condition_init_globals(), &__DCG_NO_CONDITION))

#define DCG_ELSE_CONDITION         \
    (__DCG_CONDITION_INITIALIZED   \
         ? (&__DCG_ELSE_CONDITION) \
         : (c_dcg_condition_init_globals(), &__DCG_ELSE_CONDITION))

#define DCG_AUTO_CONDITION         \
    (__DCG_CONDITION_INITIALIZED   \
         ? (&__DCG_AUTO_CONDITION) \
         : (c_dcg_condition_init_globals(), &__DCG_AUTO_CONDITION))

#define DCG_TRUE_CONDITION         \
    (__DCG_CONDITION_INITIALIZED   \
         ? (&__DCG_TRUE_CONDITION) \
         : (c_dcg_condition_init_globals(), &__DCG_TRUE_CONDITION))

#define DCG_FALSE_CONDITION         \
    (__DCG_CONDITION_INITIALIZED    \
         ? (&__DCG_FALSE_CONDITION) \
         : (c_dcg_condition_init_globals(), &__DCG_FALSE_CONDITION))

#endif  // C_DCG_BAKE_EDGE_H
