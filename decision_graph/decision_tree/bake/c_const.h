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
 * The constant family: a node that stands for a value rather than computing
 * one. The value is base.out - the family adds no field of its own - so every
 * constructor here is a base init plus the value it was given, and the string
 * flavours copy their text into a block nested under the node.
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
 * @brief A constant node: the base node, holding its value in base.out.
 *
 * The constant family carries nothing beyond the base node - the value it
 * stands for is base.out, which is why this struct has no field of its own.
 * It exists so the family has a name to construct, cast and dispatch on, and
 * so a future per-kind payload (a parsed literal, an interned string) has a
 * home that does not touch every other family.
 *
 * The base node must stay the FIRST member: a dcg_constant_node* is therefore
 * a valid dcg_node*, which is what lets the base graph API walk it.
 */
typedef struct dcg_constant_node {
    dcg_node base;  // The common node header. Must stay first.
} dcg_constant_node;

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

// ========== Lifecycle Methods ==========

/**
 * @brief Allocate a constant node of an explicit kind.
 *
 * @param ntype      A constant kind (CONST / TRUE / FALSE / DOUBLE / STRING / INT).
 * @param repr       Display text to copy (may be NULL).
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM / invalid kind.
 */
static inline dcg_constant_node* c_dcg_node_new_const(dcg_node_type ntype, const char* repr, allocator_protocol* allocator) {
    if (!c_dcg_node_type_is_const(ntype)) return NULL;

    dcg_constant_node* node = (dcg_constant_node*) c_ap_alloc(sizeof(dcg_constant_node), allocator);
    if (!node) return NULL;

    if (c_dcg_node_init(&node->base, ntype, repr) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }
    return node;
}

/**
 * @brief Tear down a constant node and free its buf.
 *
 * The value lives in the base node, so there is nothing else to release: the
 * base teardown frees the repr, the string payload and the node itself.
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
    dcg_constant_node* node = c_dcg_node_new_const(DCG_NODE_CONST, repr, allocator);
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
 * The kind follows the value (TRUE / FALSE) and so does the repr
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

#endif  // C_DCG_BAKE_CONST_H
