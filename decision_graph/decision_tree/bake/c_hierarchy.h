#ifndef C_DCG_BAKE_HIERARCHY_H
#define C_DCG_BAKE_HIERARCHY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <cbase/allocator_protocol/c_allocator_protocol.h>

#include <decision_graph/decision_tree/bake/c_node.h>

/*
 * The kinds the graph's own structure is made of:
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
 * Both own everything they hold as nested blocks, so c_dcg_node_free() unwinds
 * the whole node - there is no payload a variant hook has to release, and no
 * _free of its own to remember.
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
 * `await_connection` is the capi's field of the same name: the builder raises it
 * when the group the breakpoint belongs to is left, and lowers it when the
 * breakpoint is reached again, so a breakpoint that is still waiting is not an
 * inspection point yet. `break_from` names the group it breaks out of - NOT
 * owned, and unused until the logic group lands.
 *
 * The base node must stay the FIRST member: a dcg_breakpoint_node* is therefore
 * a valid dcg_node*.
 */
typedef struct dcg_breakpoint_node {
    dcg_node base;              // The common node header. Must stay first.
    void*    break_from;        // Currently you can ignore it, we will work on the logic group later.
    bool     await_connection;  //
} dcg_breakpoint_node;

// ========== Forward Declarations ==========

// Lifecycle
static inline dcg_root_node*       c_dcg_node_new_root(allocator_protocol* allocator);
static inline dcg_breakpoint_node* c_dcg_node_new_breakpoint(allocator_protocol* allocator);

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

#endif  // C_DCG_BAKE_HIERARCHY_H
