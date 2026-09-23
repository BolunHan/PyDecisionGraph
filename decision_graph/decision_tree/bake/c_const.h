#ifndef C_DCG_BAKE_CONST_H
#define C_DCG_BAKE_CONST_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <cbase/allocator_protocol/c_allocator_protocol.h>

#include <decision_graph/decision_tree/bake/c_node.h>

/*
 * The input family: what a graph is fed rather than what it computes. Two
 * shapes of it:
 *
 *   - a literal, which stands for a value: the family adds no field of its own,
 *     so base.out IS the value, every constructor here is a base init plus the
 *     value it was given, and the string flavours copy their text into a block
 *     nested under the node;
 *   - a variable, which stands for a value someone else holds: it names the
 *     logic group it reads and the key it reads there, and base.out refers to
 *     that entry's slot rather than holding a value of its own.
 *
 * Both are inputs because both are known to the graph from the outside - one
 * because it was written into it, the other because it is read out of a store
 * that outlives it.
 */

// ========== Constants ==========

#ifndef DCG_DEF_REPR_TRUE
#define DCG_DEF_REPR_TRUE "True"
#endif

#ifndef DCG_DEF_REPR_FALSE
#define DCG_DEF_REPR_FALSE "False"
#endif

// ========== Structs ==========

// clang-format off

/**
 * @brief A literal input: the base node, holding its value in base.out.
 *
 * A literal carries nothing beyond the base node - the value it stands for is
 * base.out, which is why this struct has no field of its own. It exists so the
 * literals have a name to construct, cast and dispatch on, and so a future
 * per-type payload (a parsed literal, an interned string) has a home that does
 * not touch every other family.
 *
 * The base node must stay the FIRST member: a dcg_constant_node* is therefore
 * a valid dcg_node*, which is what lets the base graph API walk it.
 */
typedef struct dcg_constant_node {
    dcg_node base;  // The common node header. Must stay first.
} dcg_constant_node;

/**
 * @brief A variable input: a node that reads a value out of a logic group.
 *
 * It holds no value of its own - base.out is a REFERENCE to the entry's slot,
 * so reading the variable reads that slot, live. That is what makes it usable
 * as an input to an expression (c_expr.h binds the out slot of its inputs)
 * while the value itself stays where it belongs, in the group's store.
 *
 * `key` names the entry it reads, and the node owns its copy: the key is the
 * node's, so a caller can hand one in and walk away. `logic_group` is the
 * group the entry belongs to, and is NOT owned - a group serves any number of
 * variables, so it outlives them all and is the caller's to release.
 *
 * How the entry is found is the node's OUT slot, and it changes once: a read a
 * store built is born with the entry's OFFSET in it - `VAR_TYPE_INFERRED`, so
 * nothing reads it as a value - and its first evaluation replaces that offset
 * with a reference to the entry itself. An offset rather than a pointer because
 * a store's `slots` block grows, and growing it moves every entry in it: the
 * index survives the move, and the evaluation that spends it runs before the
 * reference it leaves behind can be dangled (see
 * c_dcg_node_mapping_var_node_eval_hook).
 *
 * A variable that reads no store - one bound by hand to another node's slot -
 * carries no offset at all: its slot is the reference, from the bind on.
 *
 * The pair is what the capi's AttrExpression carries: the group gives the
 * store, the key gives the entry in it.
 *
 * The base node must stay the FIRST member: a dcg_variable_node* is therefore
 * a valid dcg_node*.
 */
typedef struct dcg_variable_node {
    dcg_node          base;         // The common node header. Must stay first.
    const char*       key;          // Entry name in the group's store.
    dcg_logic_group*  logic_group;  // The parent group whose store this reads.
} dcg_variable_node;

// clang-format on

// ========== Forward Declarations ==========

// Lifecycle
static inline dcg_constant_node* c_dcg_node_new_const(dcg_node_type ntype, const char* repr, allocator_protocol* allocator);
static inline void               c_dcg_node_free_const(dcg_constant_node* node);

static inline dcg_constant_node* c_dcg_node_new_const_value(const char* repr, dcg_var_t value, allocator_protocol* allocator);
static inline dcg_constant_node* c_dcg_node_new_const_bool(bool value, allocator_protocol* allocator);
static inline dcg_constant_node* c_dcg_node_new_const_double(double value, allocator_protocol* allocator);
static inline dcg_constant_node* c_dcg_node_new_const_int(ssize_t value, allocator_protocol* allocator);
static inline dcg_constant_node* c_dcg_node_new_const_string(const char* value, allocator_protocol* allocator);

// Reading
static inline const dcg_var_t*   c_dcg_node_const_get(const dcg_constant_node* node);
static inline int                c_dcg_node_const_set(dcg_constant_node* node, dcg_var_t value);

// Lifecycle - the variable
static inline dcg_variable_node* c_dcg_node_new_var(const char* repr, const char* key, size_t key_len, dcg_var_t* value, dcg_logic_group* group, allocator_protocol* allocator);
static inline int                c_dcg_node_var_bind(dcg_variable_node* node, dcg_var_t* value);
static inline void               c_dcg_node_free_var(dcg_variable_node* node);

// ========== Lifecycle Methods ==========

/**
 * @brief Allocate a literal input of an explicit type.
 *
 * A variable is an input too, but not one this block holds: it carries a key
 * and the group it reads, so it is built by c_dcg_node_new_var() instead.
 *
 * @param ntype      A literal type (INPUT / TRUE / FALSE / DOUBLE / STRING / INT).
 * @param repr       Display text to copy (may be NULL).
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM / invalid type.
 */
static inline dcg_constant_node* c_dcg_node_new_const(dcg_node_type ntype, const char* repr, allocator_protocol* allocator) {
    if (!c_dcg_node_type_is_input(ntype) || ntype == DCG_NODE_VARIABLE) return NULL;

    dcg_constant_node* node = (dcg_constant_node*) c_ap_alloc(sizeof(dcg_constant_node), allocator);
    if (!node) return NULL;

    if (c_dcg_node_init(&node->base, ntype, repr) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }
#if DCG_EVAL_DIRECT_HOOKS
#endif
    return node;
}

/**
 * @brief Tear down a literal and free its buf.
 *
 * The value lives in the base node, so there is nothing else to release: the
 * base teardown frees the repr, the string payload and the node itself.
 *
 * A variable is an input too, but not one this free takes: its block has a
 * different layout, so the dispatcher routes it to c_dcg_node_free_var()
 * instead of casting it to this one.
 *
 * @param node  Node to free (NULL-safe).
 */
static inline void c_dcg_node_free_const(dcg_constant_node* node) {
    if (!node) return;
    c_dcg_node_free(&node->base);
}

// ========== Public APIs - Typed Constructors ==========

/**
 * @brief Allocate a constant node holding an explicit value.
 *
 * @param repr       Display text to copy (may be NULL).
 * @param value      Constant payload (a string payload is copied).
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM.
 */
static inline dcg_constant_node* c_dcg_node_new_const_value(const char* repr, dcg_var_t value, allocator_protocol* allocator) {
    dcg_constant_node* node = c_dcg_node_new_const(DCG_NODE_INPUT, repr, allocator);
    if (!node) return NULL;

    if (c_dcg_node_const_set(node, value) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }
    return node;
}

/**
 * @brief Allocate a boolean constant.
 *
 * The type follows the value (TRUE / FALSE) and so does the repr
 * (DCG_DEF_REPR_TRUE / DCG_DEF_REPR_FALSE), so there is nothing for the caller
 * to tell it that the payload does not already say.
 *
 * @param value      Boolean payload.
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM.
 */
static inline dcg_constant_node* c_dcg_node_new_const_bool(bool value, allocator_protocol* allocator) {
    dcg_node_type      ntype = value ? DCG_NODE_TRUE : DCG_NODE_FALSE;
    dcg_constant_node* node  = c_dcg_node_new_const(ntype, value ? DCG_DEF_REPR_TRUE : DCG_DEF_REPR_FALSE, allocator);
    if (!node) return NULL;

    (void) c_dcg_var_init_bool(&node->base.out, value);
    return node;
}

/**
 * @brief Allocate a double constant, its repr rendered from the value.
 *
 * @param value      Double payload.
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM.
 */
static inline dcg_constant_node* c_dcg_node_new_const_double(double value, allocator_protocol* allocator) {
    char repr[DCG_NODE_STRING_MAXLEN];
    (void) snprintf(repr, sizeof(repr), "%g", value);

    dcg_constant_node* node = c_dcg_node_new_const(DCG_NODE_DOUBLE, repr, allocator);
    if (!node) return NULL;

    (void) c_dcg_var_init_double(&node->base.out, value);
    return node;
}

/**
 * @brief Allocate an integer constant, its repr rendered from the value.
 *
 * @param value      Integer payload.
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM.
 */
static inline dcg_constant_node* c_dcg_node_new_const_int(ssize_t value, allocator_protocol* allocator) {
    char repr[DCG_NODE_STRING_MAXLEN];
    (void) snprintf(repr, sizeof(repr), "%zd", value);

    dcg_constant_node* node = c_dcg_node_new_const(DCG_NODE_INT, repr, allocator);
    if (!node) return NULL;

    (void) c_dcg_var_init_int(&node->base.out, value);
    return node;
}

/**
 * @brief Allocate a string constant owning a copy of the text.
 *
 * The repr is the text itself, as it is for a string-valued expression.
 *
 * @param value      Text to copy (may be NULL).
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM.
 */
static inline dcg_constant_node* c_dcg_node_new_const_string(const char* value, allocator_protocol* allocator) {
    dcg_constant_node* node = c_dcg_node_new_const(DCG_NODE_STRING, value, allocator);
    if (!node) return NULL;

    if (c_dcg_node_set_string(&node->base, value) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }
    return node;
}

// ========== Public APIs - Reading ==========

/**
 * @brief The value a constant node stands for.
 *
 * @param node  Node to read (NULL-safe).
 * @return The value, or NULL.
 */
static inline const dcg_var_t* c_dcg_node_const_get(const dcg_constant_node* node) {
    if (!node) return NULL;
    return &node->base.out;
}

/**
 * @brief Replace the value a constant node stands for.
 *
 * A string value is copied into a block nested under the node, so the caller's
 * text can go away at once; the previous string value is released first.
 *
 * @param node   Node to modify.
 * @param value  New value.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_const_set(dcg_constant_node* node, dcg_var_t value) {
    if (!node) return DCG_ERR_INVALID_ARG;
    if (value.dtype == VAR_TYPE_STRING) return c_dcg_node_set_string(&node->base, value.value.as_string);
    node->base.out = value;
    return DCG_OK;
}

// ========== Public APIs - The Variable Node ==========

/**
 * @brief Allocate a node that reads an entry out of a logic group.
 *
 * The node's out is a REFERENCE to `value`, so the node reads that value live
 * and hands it to whoever reads the node - an expression bound to it, or a
 * caller inspecting the graph. `value` must outlive the node, which is what the
 * group holding it guarantees.
 *
 * A node built with no slot reflects nothing until c_dcg_node_var_bind() gives
 * it one. That is the late half of the pair: a read often has to exist before
 * the store it reads does, and refusing to build one without a slot would only
 * push the same ordering problem onto the caller.
 *
 * When `group` is given the node is allocated as a BLOCK OF THAT GROUP, so the
 * group owns the read: freeing the group releases every variable built over its
 * store, keys included, without the caller tracking them. The key is a nested
 * block of the NODE either way - a variable owns its own key, not the group -
 * so releasing the node releases the key with it.
 *
 * The two lifetimes are therefore nested - value outlives group, group outlives
 * node - and a node that must outlive its group is built with a NULL group and
 * lives on the caller's slot alone. What must NOT happen is the group going
 * while a graph still holds one of its reads: the graph names a block that is
 * gone, and nothing in the node header can tell.
 *
 * @param repr        Display text to copy (may be NULL).
 * @param key         Entry name to copy; NULL leaves the node naming no entry.
 * @param key_len     Length of key. The copy takes exactly this many bytes, so a
 *                    key that came out of a store - where a name is a pointer
 *                    and a length, not a C string - needs no termination.
 * @param value       Value slot to reflect (must outlive the node; NULL leaves
 *                    the node reflecting nothing, for c_dcg_node_var_bind()).
 * @param group       Group the entry belongs to (not owned; may be NULL, and
 *                    owning the node when it is given).
 * @param allocator   Allocator for the block; NULL derives it from the group,
 *                    or falls back to the plain heap when there is none.
 * @return The node, or NULL on OOM.
 */
static inline dcg_variable_node* c_dcg_node_new_var(const char* repr, const char* key, size_t key_len, dcg_var_t* value, dcg_logic_group* group, allocator_protocol* allocator) {
    dcg_variable_node* node = group ? (dcg_variable_node*) c_ap_alloc_child(sizeof(dcg_variable_node), allocator, group)
                                    : (dcg_variable_node*) c_ap_alloc(sizeof(dcg_variable_node), allocator);
    if (!node) return NULL;

    if (c_dcg_node_init(&node->base, DCG_NODE_VARIABLE, repr) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }

    node->logic_group = group;
    node->key         = NULL;

    if (key) {
        /* The key is the node's own copy, nested under it, so the caller's text
         * can go away at once - and it is copied before the value is bound, so
         * a failure here leaves nothing to unwind. */
        char* copy = (char*) c_ap_alloc_child(key_len + 1, NULL, node);
        if (!copy) {
            c_ap_free_owned(node);
            return NULL;
        }
        memcpy(copy, key, key_len);
        copy[key_len] = '\0';
        node->key     = copy;
    }

    if (value) (void) c_dcg_var_init_ref(&node->base.out, value);
    else (void) c_dcg_var_init(&node->base.out); /* reflects nothing yet */
    return node;
}

/**
 * @brief Give a variable the slot it reads, after it was built.
 *
 * The node's out becomes a reference to `value`, so from here on the read is
 * live: whatever the slot holds when the node is read is what the node reports.
 * Binding again simply points the read somewhere else.
 *
 * @param node   Variable node to modify.
 * @param value  Value slot to reflect (must outlive the node).
 * @return DCG_OK, or DCG_ERR_INVALID_ARG.
 */
static inline int c_dcg_node_var_bind(dcg_variable_node* node, dcg_var_t* value) {
    if (!node || !value) return DCG_ERR_INVALID_ARG;
    return c_dcg_var_init_ref(&node->base.out, value);
}

/**
 * @brief Tear down a variable node and free its buf.
 *
 * The reflected value is the store's, and the group is the caller's, so the
 * base teardown is the whole of it: it releases the repr, the key and the
 * block.
 *
 * @param node  Node to free (NULL-safe).
 */
static inline void c_dcg_node_free_var(dcg_variable_node* node) {
    if (!node) return;
    c_dcg_node_free(&node->base);
}

#endif  // C_DCG_BAKE_CONST_H
