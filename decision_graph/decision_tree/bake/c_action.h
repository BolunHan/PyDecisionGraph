#ifndef C_DCG_BAKE_ACTION_H
#define C_DCG_BAKE_ACTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <cbase/allocator_protocol/c_allocator_protocol.h>

#include <decision_graph/decision_tree/bake/c_node.h>

/*
 * The action family: the leaves that say what the graph decides (NOACTION /
 * LONGACTION / SHORTACTION / CANCELACTION / CLEARACTION), plus the placeholder
 * an unfinished branch leaves behind.
 *
 * Every one of them is the same struct - the base node and the two fields the
 * builder needs to place it: whether it should be connected to the active node
 * as it is built, and whatever data the caller hangs off it. The family's _free
 * is the base free today, because nothing it carries goes beyond the block; it
 * stays a function of its own - and the dispatcher in c_hierarchy.h routes the
 * whole family through it - because the action kinds are the ones the capi keeps
 * growing (a trade's signal, the payload a caller hangs off it), and their
 * teardown is where that growth will land. The graph's own kinds - the root and
 * the breakpoint - live in c_hierarchy.h too.
 */

// ========== Constants ==========

/*
 * Display text a kind is given when the caller does not retitle it: a build can
 * name them its own way, and the capi names the same things the same way.
 */
#ifndef DCG_DEF_REPR_PLACEHOLDER
#define DCG_DEF_REPR_PLACEHOLDER "Placeholder"
#endif

// ========== Structs ==========

/**
 * @brief An action node: the base node plus what the builder needs to place it.
 *
 * `auto_connect` is the caller's instruction, not the node's behaviour: the
 * graph builder reads it when the action is created - "connect me to the active
 * node" is what the capi does with it - and nothing in this header acts on it.
 * `action_data` is the caller's opaque pointer and is NOT owned: it is carried
 * to whoever evaluates the action, and stays the caller's to release.
 *
 * `sig` is the signal the action stands for, and it is what the capi's action
 * classes carry in the same field name: +1 for a long, -1 for a short, 0 for
 * everything that decides nothing. The kind is the authority the signal follows;
 * the variant constructors, whose kinds all decide nothing, pass 0.
 *
 * The base node must stay the FIRST member: a dcg_action_node* is therefore a
 * valid dcg_node*.
 */
typedef struct dcg_action_node {
    dcg_node base;          // The common node header. Must stay first.
    bool     auto_connect;  // Whether the builder should connect it as it is made.
    ssize_t  sig;           // The action's signal: +1 long, -1 short, 0 for the rest.
    void*    action_data;   // NOT owned - the caller's own payload.
} dcg_action_node;

// ========== Forward Declarations ==========

// Lifecycle - the action family (a kind, its repr, and its two fields)
static inline dcg_action_node* c_dcg_node_new_action(dcg_node_type action_type, const char* repr, bool auto_connect, ssize_t sig, void* action_data, allocator_protocol* allocator);
static inline void             c_dcg_node_free_action(dcg_action_node* node);

static inline dcg_action_node* c_dcg_node_new_action_trade(dcg_node_type action_type, bool auto_connect, allocator_protocol* allocator);
static inline dcg_action_node* c_dcg_node_new_action_clear(bool auto_connect, allocator_protocol* allocator);
static inline dcg_action_node* c_dcg_node_new_action_placeholder(bool auto_connect, allocator_protocol* allocator);

// ========== Lifecycle Methods ==========

/**
 * @brief Allocate an action leaf of an explicit kind.
 *
 * @param action_type   An action kind.
 * @param repr          Display text to copy (may be NULL: naming the kind is the
 *                      caller's, which is what the variants below do).
 * @param auto_connect  Whether the builder should connect it as it is made.
 * @param sig           The signal to carry (+1 long, -1 short, 0 for the rest).
 * @param action_data   The caller's payload (not owned; may be NULL).
 * @param allocator     Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM / invalid kind.
 */
static inline dcg_action_node* c_dcg_node_new_action(dcg_node_type action_type, const char* repr, bool auto_connect, ssize_t sig, void* action_data, allocator_protocol* allocator) {
    if (!c_dcg_node_type_is_action(action_type)) return NULL;

    dcg_action_node* node = (dcg_action_node*) c_ap_alloc(sizeof(dcg_action_node), allocator);
    if (!node) return NULL;

    if (c_dcg_node_init(&node->base, action_type, repr) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }

    node->auto_connect = auto_connect;
    node->sig          = sig;
    node->action_data  = action_data;
    return node;
}

/**
 * @brief Tear down an action and free its buf.
 *
 * The base free is the whole of it: `auto_connect` and `sig` are plain fields
 * inside the block, and `action_data` is the caller's own pointer, not owned.
 * It is a function of its own anyway - and the dispatcher in c_hierarchy.h
 * routes the whole family through it - because the action kinds are the ones
 * the capi keeps growing, and this is where the release of whatever they grow
 * will live.
 *
 * @param node  Node to free (NULL-safe).
 */
static inline void c_dcg_node_free_action(dcg_action_node* node) {
    if (!node) return;
    c_dcg_node_free(&node->base);
}

// ========== Public APIs - Variant Constructors ==========

/**
 * @brief Allocate a trade action of an explicit kind.
 *
 * Only the kinds that trade are accepted - a long, a short and a cancel - and
 * each brings the signal it stands for with it: +1, -1 and 0, the values the
 * capi's action classes default to. The repr is the kind's name, so the caller
 * states the decision and nothing else; anything the kind does not already say
 * the generic constructor can carry.
 *
 * @param action_type   DCG_NODE_LONGACTION, DCG_NODE_SHORTACTION or DCG_NODE_CANCELACTION.
 * @param auto_connect  Whether the builder should connect it as it is made.
 * @param allocator     Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM / a kind that does not trade.
 */
static inline dcg_action_node* c_dcg_node_new_action_trade(dcg_node_type action_type, bool auto_connect, allocator_protocol* allocator) {
    ssize_t sig = 0;

    switch (action_type) {
        case DCG_NODE_LONGACTION:
            sig = 1;
            break;
        case DCG_NODE_SHORTACTION:
            sig = -1;
            break;
        case DCG_NODE_CANCELACTION:
            break; /* neither direction: a cancel closes what is open */
        default:
            return NULL; /* a clear, a no-op and a placeholder are not trades */
    }

    return c_dcg_node_new_action(action_type, c_dcg_node_type_name(action_type), auto_connect, sig, NULL, allocator);
}

/**
 * @brief Allocate an action leaf that flattens the position.
 *
 * @param auto_connect  Whether the builder should connect it as it is made.
 * @param allocator     Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM.
 */
static inline dcg_action_node* c_dcg_node_new_action_clear(bool auto_connect, allocator_protocol* allocator) {
    return c_dcg_node_new_action(DCG_NODE_CLEARACTION, c_dcg_node_type_name(DCG_NODE_CLEARACTION), auto_connect, 0, NULL, allocator);
}

/**
 * @brief Allocate an auto-generated placeholder.
 *
 * A placeholder is what an unfinished branch leaves behind: it is flagged
 * autogen, so consolidation can tell it from an action the caller built.
 *
 * @param auto_connect  Whether the builder should connect it as it is made.
 * @param allocator     Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM.
 */
static inline dcg_action_node* c_dcg_node_new_action_placeholder(bool auto_connect, allocator_protocol* allocator) {
    dcg_action_node* node = c_dcg_node_new_action(DCG_NODE_PLACEHOLDER, DCG_DEF_REPR_PLACEHOLDER, auto_connect, 0, NULL, allocator);
    if (!node) return NULL;

    node->base.autogen = true;
    return node;
}

#endif  // C_DCG_BAKE_ACTION_H
