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
 * whole family through it - because the action types are the ones the capi keeps
 * growing (a trade's signal, the payload a caller hangs off it), and their
 * teardown is where that growth will land. The graph's own types - the root and
 * the breakpoint - live in c_hierarchy.h too.
 */

// ========== Constants ==========

/*
 * Display text a type is given when the caller does not retitle it: a build can
 * name them its own way, and the capi names the same things the same way.
 */
#ifndef DCG_DEF_REPR_PLACEHOLDER
#define DCG_DEF_REPR_PLACEHOLDER "Placeholder"
#endif

/* DCG_DEF_REPR_NOACTION is defined in c_node.h - see the note there. */

// ========== Structs ==========

/**
 * @brief An action node: the base node plus what the builder needs to place it.
 *
 * `auto_connect` records how the node was built, not what it is: the plain
 * constructors leave it false and the connect ones set it to what they were
 * asked for - "join the active node" is what the capi does with it. Nothing
 * acts on the field afterwards; it is there for whoever reads the node back.
 * `action_data` is the caller's opaque pointer and is NOT owned: it is carried
 * to whoever evaluates the action, and stays the caller's to release.
 *
 * `sig` is the signal the action stands for, and it is what the capi's action
 * classes carry in the same field name: +1 for a long, -1 for a short, 0 for
 * everything that decides nothing. The type is the authority the signal follows;
 * the variant constructors, whose types all decide nothing, pass 0.
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

/*
 * The manager a connect constructor joins through. Only the pointer is named
 * here: connecting needs the manager's stacks, which c_logic_group.h defines,
 * and that header includes this one - so the bodies live there and this header
 * carries the family's shape.
 */
typedef struct dcg_logic_group_manager dcg_logic_group_manager;

// Lifecycle - the action family (a type, its repr, and its two fields)
static inline dcg_action_node*         c_dcg_node_new_action(dcg_node_type action_type, const char* repr, ssize_t sig, void* action_data, allocator_protocol* allocator);
static inline void                     c_dcg_node_free_action(dcg_action_node* node);

static inline dcg_action_node*         c_dcg_node_new_action_trade(dcg_node_type action_type, allocator_protocol* allocator);
static inline dcg_action_node*         c_dcg_node_new_action_clear(allocator_protocol* allocator);
static inline dcg_action_node*         c_dcg_node_new_action_placeholder(allocator_protocol* allocator);

/*
 * The connect variants of the four constructors above - the ones that take a
 * manager and join the node to the active one - are declared and defined in
 * c_logic_group.h, next to each other: a static inline function has to be
 * defined in every translation unit that declares it, and only that header
 * knows what a manager is.
 */

// Closing a branch
static inline dcg_node*                c_dcg_node_get_placeholder(dcg_node* node);
static inline int                      c_dcg_node_auto_fill(dcg_node* node);

// ========== Lifecycle Methods ==========

/**
 * @brief Allocate an action leaf of an explicit type - standalone.
 *
 * The node joins no graph here: connecting means taking the arm the active
 * node reserved, and that needs a manager to say which node is active. These
 * constructors build a node nobody owns yet; the connect variants below take
 * the manager and do the joining.
 *
 * @param action_type   An action type.
 * @param repr          Display text to copy (may be NULL: naming the type is the
 *                      caller's, which is what the variants below do).
 * @param sig           The signal to carry (+1 long, -1 short, 0 for the rest).
 * @param action_data   The caller's payload (not owned; may be NULL).
 * @param allocator     Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM / invalid type.
 */
static inline dcg_action_node*         c_dcg_node_new_action(dcg_node_type action_type, const char* repr, ssize_t sig, void* action_data, allocator_protocol* allocator) {
    if (!c_dcg_node_type_is_action(action_type)) return NULL;

    dcg_action_node* node = (dcg_action_node*) c_ap_alloc(sizeof(dcg_action_node), allocator);
    if (!node) return NULL;

    if (c_dcg_node_init(&node->base, action_type, repr) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }

    node->auto_connect = false; /* nothing was connected: the callers that do say so */
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
 * routes the whole family through it - because the action types are the ones
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
 * @brief Allocate a trade action of an explicit type.
 *
 * Only the types that trade are accepted - a long, a short and a cancel - and
 * each brings the signal it stands for with it: +1, -1 and 0, the values the
 * capi's action classes default to. The repr is the type's name, so the caller
 * states the decision and nothing else; anything the type does not already say
 * the generic constructor can carry.
 *
 * @param action_type   DCG_NODE_LONGACTION, DCG_NODE_SHORTACTION or DCG_NODE_CANCELACTION.
 * @param allocator     Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM / a type that does not trade.
 */
static inline dcg_action_node* c_dcg_node_new_action_trade(dcg_node_type action_type, allocator_protocol* allocator) {
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

    return c_dcg_node_new_action(action_type, c_dcg_node_type_name(action_type), sig, NULL, allocator);
}

/**
 * @brief Allocate an action leaf that flattens the position.
 *
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM.
 */
static inline dcg_action_node* c_dcg_node_new_action_clear(allocator_protocol* allocator) {
    return c_dcg_node_new_action(DCG_NODE_CLEARACTION, c_dcg_node_type_name(DCG_NODE_CLEARACTION), 0, NULL, allocator);
}

/**
 * @brief Allocate an auto-generated placeholder.
 *
 * A placeholder is what an unfinished branch leaves behind: it is flagged
 * autogen, so consolidation can tell it from an action the caller built.
 *
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM.
 */
static inline dcg_action_node* c_dcg_node_new_action_placeholder(allocator_protocol* allocator) {
    dcg_action_node* node = c_dcg_node_new_action(DCG_NODE_PLACEHOLDER, DCG_DEF_REPR_PLACEHOLDER, 0, NULL, allocator);
    if (!node) return NULL;

    node->base.autogen = true;
    return node;
}

/**
 * @brief The placeholder slot under a node: the one already reserved, or a new one.
 *
 * The opening half of the placeholder discipline - the closing half is
 * c_dcg_node_consolidate_placeholder(). A branch that is about to be built
 * reserves its slot first, and whatever fills the slot replaces the
 * placeholder; a slot nothing ever fills becomes a no-action when the graph
 * closes.
 *
 * The rules are the capi's, in its order:
 *
 *   - the NEWEST existing placeholder is reused, so a build that descends into
 *     a node fills the slot that was reserved last rather than reserving a
 *     second. Newest and not first, because a node entered by a `with` block
 *     reserves its two arms in the order FALSE then TRUE, and the capi - whose
 *     stack puts the newest at index 0 - fills the TRUE arm first. Taking the
 *     first placeholder here would fill the arms in the opposite order and
 *     build a different graph;
 *   - otherwise a placeholder is appended on the INFERRED edge, which is what
 *     makes this fail exactly when the node has no room left for a branch -
 *     a node with one non-binary child and no free edge, say. A root is no
 *     exception: the inference hands back the unconditioned edge it requires.
 *
 * The placeholder is allocated from the same allocator the node came from, so
 * the two are one allocation family; it is NOT nested under the node, because
 * a node does not own its children (see c_dcg_node_free).
 *
 * @param node  Node to reserve a slot under (NULL-safe).
 * @return The placeholder, or NULL on OOM / when no edge can be inferred.
 */
static inline dcg_node* c_dcg_node_get_placeholder(dcg_node* node) {
    if (!node) return NULL;

    dcg_node* newest = NULL;
    for (dcg_node* child = node->children; child; child = child->next_sibling) {
        if (child->ntype == DCG_NODE_PLACEHOLDER) newest = child;
    }
    if (newest) return newest;

    dcg_action_node* placeholder = c_dcg_node_new_action_placeholder(c_ap_protocol_from_ptr(node));
    if (!placeholder) return NULL;

    if (c_dcg_node_append_auto(node, &placeholder->base) != DCG_OK) {
        c_dcg_node_free_action(placeholder);
        return NULL;
    }
    return &placeholder->base;
}

// ========== Public APIs - Closing a Branch ==========

/**
 * @brief Close a node's branches: fill the arm it never got, add the fallback.
 *
 * The step a build closes a node with, and the second half of the placeholder
 * discipline - a node is entered with its arms reserved (c_dcg_node_get_placeholder),
 * a build fills the arms it means to, and this supplies the ones it did not.
 * It runs on exit, before the placeholders are consolidated.
 *
 * Every arm it fills is filled with an auto-generated no-action, so a later
 * consolidation can tell it from an action the caller wrote. The rules read the
 * node's EXISTING branches, in the capi's order:
 *
 *   - no branch at all  -> an auto no-action on the unconditioned edge;
 *   - one branch        -> its opposite when it is a binary, nothing when it is
 *                          unconditioned, and the TRUE arm an ELSE-only node
 *                          was waiting for (inserted in front, so the fallback
 *                          stays last) - a value-keyed arm stands alone;
 *   - two branches      -> two binaries stand, an ELSE stands as the fallback,
 *                          two value-keyed arms gain a protective ELSE, and any
 *                          other pairing is contradictory (DCG_ERR_EDGE);
 *   - three or more     -> binaries and unconditioned edges are illegal among
 *                          them (DCG_ERR_TYPE or DCG_ERR_EDGE), and a set with
 *                          no fallback gains a trailing ELSE.
 *
 * Nothing is added when the node already says all it can: the call is
 * idempotent, and a failure changes nothing - the auto no-action is released
 * rather than left half-linked.
 *
 * @param node  Node to close (NULL-safe).
 * @return DCG_OK, or a DCG_ERR_* code: DCG_ERR_INVALID_ARG (NULL), DCG_ERR_EDGE
 *         (contradictory branches), DCG_ERR_TYPE (too many, or a binary among
 *         three) and DCG_ERR_OOM.
 */
static inline int c_dcg_node_auto_fill(dcg_node* node) {
    if (!node) return DCG_ERR_INVALID_ARG;

    size_t           count = c_dcg_node_child_count(node);
    dcg_node*        last  = c_dcg_node_last_child(node);

    /* The arm every branch below is filled with, built on demand and released
     * again if the link it was built for is refused. */
    dcg_action_node* no_action = NULL;
    int              ret       = DCG_OK;

    if (count == 0) {
        no_action = c_dcg_node_new_action(DCG_NODE_NOACTION, DCG_DEF_REPR_NOACTION, 0, NULL, c_ap_protocol_from_ptr(node));
        if (!no_action) return DCG_ERR_OOM;
        no_action->base.autogen = true;

        ret = c_dcg_node_append(node, &no_action->base, DCG_NO_CONDITION);
        if (ret != DCG_OK) c_dcg_node_free_action(no_action);
        return ret;
    }

    const dcg_node_edge_condition* condition = last->condition_to_parent;

    if (count == 1) {
        const dcg_node_edge_condition* fill = NULL;
        bool                           head = false; /* an ELSE waits for its TRUE arm, which goes in front */

        if (c_dcg_condition_is_none(condition)) return DCG_OK;
        else if (c_dcg_condition_is_binary(condition)) fill = c_dcg_condition_is_true(condition) ? DCG_FALSE_CONDITION : DCG_TRUE_CONDITION;
        else if (c_dcg_condition_is_else(condition)) {
            fill = DCG_TRUE_CONDITION;
            head = true;
        }
        else return DCG_OK; /* a value-keyed arm stands alone */

        no_action = c_dcg_node_new_action(DCG_NODE_NOACTION, DCG_DEF_REPR_NOACTION, 0, NULL, c_ap_protocol_from_ptr(node));
        if (!no_action) return DCG_ERR_OOM;
        no_action->base.autogen = true;

        ret = head ? c_dcg_node_append_at(node, &no_action->base, fill, 0) : c_dcg_node_append(node, &no_action->base, fill);
        if (ret != DCG_OK) c_dcg_node_free_action(no_action);
        return ret;
    }

    if (count == 2) {
        const dcg_node_edge_condition* second_condition = last->prev_sibling->condition_to_parent;

        if (c_dcg_condition_is_none(condition) || c_dcg_condition_is_none(second_condition)) return DCG_ERR_EDGE; /* an unconditioned arm cannot share a node */
        if (c_dcg_condition_is_else(condition) || c_dcg_condition_is_else(second_condition)) return DCG_OK;
        if (c_dcg_condition_is_binary(condition) && c_dcg_condition_is_binary(second_condition)) return DCG_OK;
        if (c_dcg_condition_is_binary(condition) || c_dcg_condition_is_binary(second_condition)) return DCG_ERR_EDGE; /* a binary beside a value-keyed arm */
        /* Two value-keyed arms: the fallback is what makes the node total. */
    }
    else {
        /* Three or more: only value-keyed arms may share a node. */
        for (dcg_node* child = node->children; child; child = child->next_sibling) {
            const dcg_node_edge_condition* child_condition = child->condition_to_parent;

            if (c_dcg_condition_is_none(child_condition)) return DCG_ERR_EDGE;
            if (c_dcg_condition_is_binary(child_condition)) return DCG_ERR_TYPE;
            if (c_dcg_condition_is_else(child_condition)) return DCG_OK; /* the fallback is there already, and it is last */
        }
    }

    no_action = c_dcg_node_new_action(DCG_NODE_NOACTION, DCG_DEF_REPR_NOACTION, 0, NULL, c_ap_protocol_from_ptr(node));
    if (!no_action) return DCG_ERR_OOM;
    no_action->base.autogen = true;

    ret = c_dcg_node_append(node, &no_action->base, DCG_ELSE_CONDITION);
    if (ret != DCG_OK) c_dcg_node_free_action(no_action);
    return ret;
}

#endif  // C_DCG_BAKE_ACTION_H