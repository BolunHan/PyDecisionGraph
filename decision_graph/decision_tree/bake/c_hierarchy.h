#ifndef C_DCG_BAKE_HIERARCHY_H
#define C_DCG_BAKE_HIERARCHY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <cbase/allocator_protocol/c_allocator_protocol.h>

#include <decision_graph/decision_tree/bake/c_action.h>
#include <decision_graph/decision_tree/bake/c_const.h>
#include <decision_graph/decision_tree/bake/c_expr.h>
#include <decision_graph/decision_tree/bake/c_node.h>

/*
 * The kinds the graph's own structure is made of, and the one way a graph is
 * torn down:
 *
 *   - the root: the one node that may have no parent, and the entry a walk
 *     starts from. Its value is true, so a branch can be read off it directly,
 *     and it carries what a walk needs from an entry - whether it inherits the
 *     contexts it is entered from, how deep it may go, and the record of the
 *     path it took;
 *   - the breakpoint: an inspection sink. It carries the eval breakpoint flag
 *     and the two fields its group logic needs - the group it breaks from, and
 *     whether it is still waiting to be connected back.
 *
 * Both own everything they hold as nested blocks - a root's eval path included
 * - so their _free is the base teardown and nothing else goes with them. What
 * is NOT local is the graph: c_dcg_node_teardown_root() is the single place a
 * graph is walked and freed, leaf first, and c_dcg_node_free_generic() is the
 * step it takes per node - the free of whatever kind that node turns out to be.
 */

// ========== Constants ==========

/*
 * Display text a kind is given when the caller does not retitle it: a build can
 * name them its own way, and the capi names the same things the same way.
 */
#ifndef DCG_DEF_REPR_ROOT
#define DCG_DEF_REPR_ROOT "Entry Point"
#endif

#ifndef DCG_DEF_REPR_BREAKPOINT
#define DCG_DEF_REPR_BREAKPOINT "Breakpoint"
#endif

/** Nodes the teardown record starts with; it doubles as the walk grows. */
#ifndef DCG_TEARDOWN_INITIAL_CAPACITY
#define DCG_TEARDOWN_INITIAL_CAPACITY 16U
#endif

// ========== Structs ==========

/**
 * @brief The path a walk took through the graph: the nodes and their values.
 *
 * One entry per node visited, in order: the node and a snapshot of the value it
 * held when it was left. It is the bake counterpart of the capi's eval path,
 * which is what a caller reads back to see how a decision was reached.
 *
 * Both blocks are nested under the root that owns the path, so freeing the root
 * releases the record with it; the path starts empty and is grown by whatever
 * walks the graph.
 */
typedef struct dcg_node_eval_path {
    dcg_node** node;      // eval node
    dcg_var_t* eval_val;  // eval node value snapshot
    size_t     capacity;
    size_t     n_nodes;
} dcg_node_eval_path;

/**
 * @brief The graph entry point: the base node plus what a walk starts with.
 *
 * `inherit_contexts` mirrors the capi's field of the same name: whether the
 * entry takes the contexts of the groups it is entered from with it (its
 * default, as there, is not to). `max_depth` caps how far a walk may descend,
 * 0 meaning the whole graph. `eval_path` is the record the walk fills in.
 *
 * The base node must stay the FIRST member: a dcg_root_node* is therefore a
 * valid dcg_node*.
 */
typedef struct dcg_root_node {
    dcg_node           base;  // The common node header. Must stay first.
    bool               inherit_contexts;
    size_t             max_depth;
    dcg_node_eval_path eval_path;
} dcg_root_node;

/**
 * @brief An inspection sink: the base node plus its group's two fields.
 *
 * `await_connection` is the capi's field of the same name: the manager raises it
 * when the group the breakpoint belongs to is left, and lowers it when the
 * breakpoint is connected to the node that resumes outside it, so a breakpoint
 * that is still waiting is not an inspection point yet.
 *
 * `break_from` names the group it breaks out of. It is NOT owned - a group
 * outlives every breakpoint raised from it - and it is what the manager matches
 * on when a group is left (see c_dcg_lgm_exit_group).
 *
 * The base node must stay the FIRST member: a dcg_breakpoint_node* is therefore
 * a valid dcg_node*.
 */
typedef struct dcg_breakpoint_node {
    dcg_node         base;              // The common node header. Must stay first.
    dcg_logic_group* break_from;        // The group this breaks out of. // NOT owned.
    bool             await_connection;  // Waiting for the node it resumes into.
} dcg_breakpoint_node;

// ========== Forward Declarations ==========

// Lifecycle
static inline dcg_root_node*       c_dcg_node_new_root(allocator_protocol* allocator);
static inline dcg_breakpoint_node* c_dcg_node_new_breakpoint(allocator_protocol* allocator);
static inline void                 c_dcg_node_free_root(dcg_root_node* node);
static inline void                 c_dcg_node_free_breakpoint(dcg_breakpoint_node* node);

// Teardown - freeing a node, and freeing a whole graph
static inline void                 c_dcg_node_free_generic(dcg_node* node);
static inline int                  c_dcg_node_teardown_root(dcg_node* node);
static inline int                  c_dcg_node_remove(dcg_node* node);
static inline int                  c_dcg_node_clear_children(dcg_node* node);
static inline int                  c_dcg_node_clean(dcg_node* node);

// ========== Lifecycle Methods ==========

/**
 * @brief Allocate the graph entry point.
 *
 * The value is true - the entry is taken, which is what lets a branch be read
 * off the root directly - and the walk's record starts empty: nothing has been
 * walked yet, so there is no path to report and no block to own.
 *
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM.
 */
static inline dcg_root_node*       c_dcg_node_new_root(allocator_protocol* allocator) {
    dcg_root_node* node = (dcg_root_node*) c_ap_alloc(sizeof(dcg_root_node), allocator);
    if (!node) return NULL;

    if (c_dcg_node_init(&node->base, DCG_NODE_ROOT, DCG_DEF_REPR_ROOT) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }
    (void) c_dcg_var_init_bool(&node->base.out, true);

    node->inherit_contexts = false; /* the capi's default: take nothing with it */
    node->max_depth        = 0;     /* 0 walks the whole graph */

    node->eval_path.node     = NULL;
    node->eval_path.eval_val = NULL;
    node->eval_path.capacity = 0;
    node->eval_path.n_nodes  = 0;
    return node;
}

/**
 * @brief Allocate a breakpoint sink.
 *
 * The breakpoint flag is set here, so an evaluator that walks the graph stops at
 * this node without having to know which nodes are inspection points. The group
 * fields start empty: nothing has been entered or left yet.
 *
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM.
 */
static inline dcg_breakpoint_node* c_dcg_node_new_breakpoint(allocator_protocol* allocator) {
    dcg_breakpoint_node* node = (dcg_breakpoint_node*) c_ap_alloc(sizeof(dcg_breakpoint_node), allocator);
    if (!node) return NULL;

    if (c_dcg_node_init(&node->base, DCG_NODE_BREAKPOINT, DCG_DEF_REPR_BREAKPOINT) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }
    node->base.eval_ctx.flags |= DCG_EVAL_FLAG_BREAKPOINT;

    node->base.autogen     = true;
    node->await_connection = false; /* not left yet, so nothing to wait for */
    node->break_from       = NULL;  /* and no group to break out of */
    return node;
}

/**
 * @brief Tear down a root and free its buf.
 *
 * The eval path is a pair of blocks nested under the root, so the base free
 * releases the record with the node - there is nothing else to release, and
 * the children are let go like every other node's (see c_dcg_node_dealloc).
 *
 * @param node  Node to free (NULL-safe).
 */
static inline void c_dcg_node_free_root(dcg_root_node* node) {
    if (!node) return;
    c_dcg_node_free(&node->base);
}

/**
 * @brief Tear down a breakpoint and free its buf.
 *
 * The group fields name blocks the breakpoint does not own, so the base free
 * is the whole of it.
 *
 * @param node  Node to free (NULL-safe).
 */
static inline void c_dcg_node_free_breakpoint(dcg_breakpoint_node* node) {
    if (!node) return;
    c_dcg_node_free(&node->base);
}

// ========== Teardown ==========

/**
 * @brief Free a node whose kind is only known at run time.
 *
 * c_dcg_node_free() releases the base half of a node; a family that puts state
 * after the base header releases that state in its own _free. This is the one
 * place that knows both: the node's own `ntype` is the dispatch flag, and the
 * free of the kind it names is the one called - a literal and a variable
 * through their two input frees, the operator family through
 * c_dcg_node_free_expr(), the whole action family through
 * c_dcg_node_free_action(), a root and a breakpoint through theirs.
 *
 * A family whose kinds have different layouts is split here, not inside its own
 * free: a variable is not a literal, and a free that took one as the other
 * would be reading a block that is not that shape.
 *
 * A kind this dispatcher has no free for - a variant it has never heard of -
 * is reported on stderr, and its base is released all the same: the silent
 * alternative would hand back half a node and leak whatever the kind put
 * behind the base, while the caller that built such a node is the one that
 * can fix it.
 *
 * Like every other free, this is LOCAL: the node's children are let go, not
 * freed. Walk and free a whole graph with c_dcg_node_teardown_root().
 *
 * @param node  Node to free (NULL-safe).
 */
static inline void c_dcg_node_free_generic(dcg_node* node) {
    if (!node) return;

    switch ((int) node->ntype & DCG_NODE_FAMILY_MASK) {
        case DCG_NODE_INPUT:
            if (node->ntype == DCG_NODE_VARIABLE) {
                c_dcg_node_free_var((dcg_variable_node*) node);
                return;
            }
            c_dcg_node_free_const((dcg_constant_node*) node);
            return;
        case DCG_NODE_OP:
            c_dcg_node_free_expr((dcg_expression_node*) node);
            return;
        case DCG_NODE_ACTION:
            c_dcg_node_free_action((dcg_action_node*) node);
            return;
        case DCG_NODE_SPECIAL:
            if (node->ntype == DCG_NODE_ROOT) {
                c_dcg_node_free_root((dcg_root_node*) node);
                return;
            }
            if (node->ntype == DCG_NODE_BREAKPOINT) {
                c_dcg_node_free_breakpoint((dcg_breakpoint_node*) node);
                return;
            }
            break;
        default:
            break;
    }

    /* Every kind with a free of its own returned above, so getting here means
     * the kind is one this dispatcher cannot take apart. Say so: a node like
     * that is a bug in whoever built it, and the message is the only trace the
     * caller gets. */
    (void) fprintf(stderr, "c_dcg_node_free_generic: no free for node kind %s (0x%04x) at %p - releasing the base only\n", c_dcg_node_type_name(node->ntype), (unsigned) node->ntype, (const void*) node);
    c_dcg_node_free(node);
}

/**
 * @brief Tear down a whole graph from its top: every node, leaf first.
 *
 * The one place a graph is freed. It starts from a node that has no parent -
 * the graph's ROOT, or any parentless fragment top - because a node that still
 * has a parent is part of somebody else's graph (a sub-root kept for regional
 * management): tearing it down from there would free blocks the graph above
 * still points at. A node with a parent is refused with DCG_ERR_BUSY and
 * nothing is touched.
 *
 * The walk is breadth-first over a record of the nodes it reached, so the graph
 * is not assumed to be a tree: a node reached twice (an interlink - a child of
 * two parents, only one of which its metadata logs) is recorded once, and a
 * back edge cannot make the walk spin. Every edge of the record is then
 * dropped, and the nodes are freed in the reverse of the record, which puts
 * every child before its parent - so nothing a free walks into is already
 * gone, and the allocator never sees a block that still owns children.
 *
 * Collecting before freeing is also what makes the failure path safe: when the
 * record cannot be added to, nothing has been freed yet.
 *
 * What the record cannot do is make sharing safe: a node this graph reaches is
 * freed, and a graph that was NOT torn down yet still holds it - which is the
 * caller's interlink to keep track of, exactly as it is in the capi.
 *
 * @param node  Top of the graph to tear down: a node without a parent.
 * @return The number of nodes freed (1 or more), or a DCG_ERR_* code:
 *         DCG_ERR_INVALID_ARG (NULL), DCG_ERR_BUSY (the node has a parent),
 *         DCG_ERR_OOM (the record could not be allocated).
 */
static inline int c_dcg_node_teardown_root(dcg_node* node) {
    if (!node) return DCG_ERR_INVALID_ARG;
    if (node->parent) return DCG_ERR_BUSY; /* somebody else's subtree - not a top */

    allocator_protocol* allocator = c_ap_protocol_from_ptr(node);
    size_t              capacity  = DCG_TEARDOWN_INITIAL_CAPACITY;
    size_t              count     = 0;
    dcg_node**          order     = (dcg_node**) c_ap_alloc_child(capacity * sizeof(dcg_node*), allocator, node);
    if (!order) return DCG_ERR_OOM;

    /* Collect: the root, then the children of every node collected, each node
     * once. Nothing is freed until the whole graph is on the record. */
    order[count++] = node;
    node->flags |= DCG_NODE_FLAG_VISITED;

    for (size_t head = 0; head < count; head++) {
        for (dcg_node* child = order[head]->children; child; child = child->next_sibling) {
            if (child->flags & DCG_NODE_FLAG_VISITED) continue; /* interlink or back edge */
            child->flags |= DCG_NODE_FLAG_VISITED;

            if (count == capacity) {
                size_t     grown_capacity = capacity * 2;
                dcg_node** grown          = (dcg_node**) c_ap_realloc(order, grown_capacity * sizeof(dcg_node*), allocator);
                if (!grown) {
                    /* Nothing has been freed: put the marks back and leave the graph be. */
                    for (size_t i = 0; i < count; i++) order[i]->flags &= ~(uint32_t) DCG_NODE_FLAG_VISITED;
                    c_ap_free_owned(order);
                    return DCG_ERR_OOM;
                }
                order    = grown;
                capacity = grown_capacity;
            }
            order[count++] = child;
        }
    }

    /* Unwire the record before anything is freed. Every node on it is about to
     * go, and dropping the edges first means no free can walk into a node that
     * is already gone - which a back edge, or a child two parents reach, would
     * otherwise make possible.
     *
     * Two passes, in this order: an unlink hands the removed node's NEXT
     * sibling to the parent's child list, so a list can gain an entry while the
     * unwiring runs - and it must be dropped after, not before, that happens.
     * Once both passes are done, every node of the record is a lone block. */
    for (size_t i = 0; i < count; i++) c_dcg_node_unlink(order[i]);
    for (size_t i = 0; i < count; i++) order[i]->children = NULL;

    /* Free leaf first: the reverse of the record puts every child before its
     * parent, so the allocator never sees a block that still owns children.
     * The record is a block nested under the top, so the last free of all
     * releases it. */
    for (size_t i = count; i-- > 0;) c_dcg_node_free_generic(order[i]);

    return (int) count;
}

/**
 * @brief Unlink a node and tear its whole subtree down.
 *
 * The node leaves the graph first - its edge is dropped and the former parent
 * is told - and the detached subtree is then torn down from its top, which is
 * what lets a branch be removed without a parent check anywhere.
 *
 * @param node  Node to remove (NULL-safe: a detached node is a no-op tree).
 * @return The number of nodes freed, or a DCG_ERR_* code: DCG_ERR_BUSY when the
 *         parent is FROZEN, DCG_ERR_OOM when the teardown could not collect -
 *         in which case the node is left detached, with its subtree intact.
 */
static inline int c_dcg_node_remove(dcg_node* node) {
    if (!node) return DCG_OK;
    if (node->parent && (node->parent->flags & DCG_NODE_FLAG_FROZEN)) return DCG_ERR_BUSY;

    c_dcg_node_unlink(node); /* silent: the subtree is about to go anyway */
    return c_dcg_node_teardown_root(node);
}

/**
 * @brief Drop every child of a node, tearing their subtrees down.
 *
 * Each child is unlinked first - so the list cannot be mutated under the walk -
 * and its subtree is then torn down. A teardown that could not collect its
 * record (OOM) leaves that child detached but alive, and the children after it
 * still attached; the CLEARED event is only announced when every child went.
 *
 * @param node  Node to clear (NULL-safe). FROZEN nodes are left untouched.
 * @return The number of nodes freed with the dropped subtrees, or the first
 *         DCG_ERR_* a teardown failed with.
 */
static inline int c_dcg_node_clear_children(dcg_node* node) {
    if (!node || !node->children) return 0;
    if (node->flags & DCG_NODE_FLAG_FROZEN) return 0;

    int freed = 0;
    int error = DCG_OK;

    while (node->children) {
        dcg_node* child = node->children;
        c_dcg_node_unlink(child); /* out of the list before its subtree goes */

        int child_result = c_dcg_node_teardown_root(child);
        if (child_result < 0) {
            if (error == DCG_OK) error = child_result;
        }
        else {
            freed += child_result;
        }
    }

    if (error != DCG_OK) return error; /* a child that could not be collected */

    c_dcg_node_invoke_callbacks(node, DCG_NODE_EVENT_CLEARED, node, (uint64_t) -1);
    return freed;
}

/**
 * @brief Back to a fresh state, keeping the node's identity and bindings.
 *
 * Drops the subtree and the labels, resets the value slot and the eval
 * scratch, but KEEPS: the kind, the operator, the repr and its ownership
 * flag, the uid, the eval hooks, the mutation callbacks and the
 * user_payload. This is what a builder calls to reuse a node block.
 *
 * The CLEARED event is announced once: by the child drop when there were
 * children to drop, and by the reset itself when there were none.
 *
 * @param node  Node to clean (NULL-safe).
 * @return The number of nodes freed with the dropped subtrees, or the first
 *         DCG_ERR_* a teardown failed with - the reset itself always happens.
 */
static inline int c_dcg_node_clean(dcg_node* node) {
    if (!node) return DCG_ERR_INVALID_ARG;

    bool            had_children = node->children != NULL;
    int             ret          = c_dcg_node_clear_children(node);

    dcg_node_label* label = node->labels;
    node->labels          = NULL;
    while (label) {
        dcg_node_label* next = label->next;
        c_ap_free_owned(label);
        label = next;
    }

    if (node->out.dtype == VAR_TYPE_STRING && node->out.value.as_string) {
        c_ap_free_owned((void*) node->out.value.as_string);
    }
    (void) c_dcg_var_init(&node->out);

    node->eval_ctx.run    = NULL;
    node->eval_ctx.flags  = DCG_EVAL_FLAG_NONE;
    node->eval_ctx.depth  = 0;
    node->eval_ctx.visits = 0;

    if (ret >= 0 && !had_children) c_dcg_node_invoke_callbacks(node, DCG_NODE_EVENT_CLEARED, node, (uint64_t) -1);
    return ret;
}

#endif  // C_DCG_BAKE_HIERARCHY_H
