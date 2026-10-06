#ifndef C_DCG_BAKE_HIERARCHY_H
#define C_DCG_BAKE_HIERARCHY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <cbase/allocator_protocol/c_allocator_protocol.h>

#include <decision_graph/decision_tree/bake/c_action.h>
#include <decision_graph/decision_tree/bake/c_const.h>
#include <decision_graph/decision_tree/bake/c_node.h>

/*
 * The expression family, DECLARED rather than included: its include sits at the
 * bottom (see the note there), and these two are all this header's body names
 * before it - the family's node as a cast, and the family's own free. Both are
 * satisfied by a pointer, so the incomplete type is enough.
 */
typedef struct dcg_expression_node dcg_expression_node;
static inline void                 c_dcg_node_free_expr(dcg_expression_node* node);

/*
 * The types the graph's own structure is made of, and the one way a graph is
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
 * step it takes per node - the free of whatever type that node turns out to be.
 */

// ========== Constants ==========

/*
 * Display text a type is given when the caller does not retitle it: a build can
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
 * The record is DATA - two arrays and the outcome of the walk - and it owns
 * neither the nodes it names nor the graph they belong to. Where its blocks come
 * from is the caller's to decide, and the two ways are the two ways a record is
 * used:
 *
 *   - a record of its own, from c_dcg_node_eval_path_new(): one block holding
 *     the arrays, released with it;
 *   - the record embedded in a root (dcg_root_node.eval_path), whose blocks are
 *     nested under the root and released with it - which is why the root's own
 *     walk appends through a variant of its own.
 *
 * The outcome of the walk is part of the record, not something a caller has to
 * infer from the entries: `code` is what the walk ended with (DCG_OK when it
 * reached a leaf), `leaf` the node it landed on, `failed` the node that reported
 * the failure when there was one, and `seq_id` the run the record belongs to.
 * `node` and `eval_val` are the entries themselves, `capacity` the room the pair
 * of blocks has and `n_nodes` how much of it is used - and a record that needs
 * more room takes it, so no walk is ever recorded in part (see
 * c_dcg_eval_path_append).
 *
 * A snapshot is a SHALLOW copy of the slot as it stood: a payload the node owns
 * is not copied, so the record borrows it - valid for as long as the node is not
 * evaluated again, which is the whole of the run that wrote it.
 */
typedef struct dcg_node_eval_path {
    dcg_node**   node;      // eval node
    dcg_var_t*   eval_val;  // eval node value snapshot
    size_t       capacity;  // Entries the blocks have room for.
    size_t       n_nodes;   // Entries in use.
    dcg_ret_code code;      // Outcome of the walk: DCG_OK when it reached a leaf.
    dcg_node*    leaf;      // The node the walk landed on; NULL when it did not land.
    dcg_node*    failed;    // The node that reported the failure, when one did.
    uint64_t     seq_id;    // The run this record belongs to.
} dcg_node_eval_path;

/**
 * @brief The graph entry point: the base node plus what a walk starts with.
 *
 * `eval_path` is that record - the nodes a walk visited and the value each one
 * produced. `inherit_contexts` is the capi's field of the same name: whether
 * the entry takes the contexts of the groups it is entered from in with it, its
 * default (as there) being not to. It is what chooses between the two halves of
 * the shelving a root's enter performs, and only the root reads it.
 *
 * The base node must stay the FIRST member: a dcg_root_node* is therefore a
 * valid dcg_node*.
 */
typedef struct dcg_root_node {
    dcg_node           base;  // The common node header. Must stay first.
    dcg_node_eval_path eval_path;
    bool               inherit_contexts;
} dcg_root_node;

/**
 * @note What entering a root does to the manager's stacks, and its undo, are
 * declared AND defined in c_logic_group.h - the only header that can reach the
 * shelving. They cannot be declared here: a `static inline` must be defined in
 * every translation unit that declares it, and a unit that includes this header
 * for the node types has no reason to include the manager's.
 */

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
static inline dcg_root_node*       c_dcg_node_new_root(const char* repr, allocator_protocol* allocator);
static inline int                   c_dcg_node_ctx_enter_breakpoint(dcg_node* node, dcg_logic_group_manager* mgr);
static inline dcg_breakpoint_node* c_dcg_node_new_breakpoint(dcg_logic_group* break_from, const char* repr, allocator_protocol* allocator);
static inline dcg_breakpoint_node* c_dcg_node_get_breakpoint(const dcg_node* node);
static inline int                   c_dcg_node_resolve_breakpoint(dcg_node* node);
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
 * @param repr       Display text to copy; NULL takes the root's own default.
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM.
 */
static inline dcg_root_node*       c_dcg_node_new_root(const char* repr, allocator_protocol* allocator) {
    dcg_root_node* node = (dcg_root_node*) c_ap_alloc(sizeof(dcg_root_node), allocator);
    if (!node) return NULL;

    if (c_dcg_node_init(&node->base, DCG_NODE_ROOT, repr ? repr : DCG_DEF_REPR_ROOT) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }
    (void) c_dcg_var_init_bool(&node->base.out, true);

    /* The record starts with no room and no blocks: they are nested under this
     * node the first time a walk records into it, and released with it - see
     * c_dcg_root_node_eval_path_append in c_eval.h. */
    node->eval_path.node     = NULL;
    node->eval_path.eval_val = NULL;
    node->eval_path.capacity = 0;
    node->eval_path.n_nodes  = 0;
    node->eval_path.code     = DCG_OK;
    node->eval_path.leaf     = NULL;
    node->eval_path.failed   = NULL;
    node->eval_path.seq_id   = 0;
    return node;
}

/**
 * @brief Allocate a breakpoint sink.
 *
 * The breakpoint flag is set here, so an evaluator that walks the graph stops at
 * this node without having to know which nodes are inspection points. The group
 * fields start empty: nothing has been entered or left yet.
 *
 * @param break_from  The group this breaks out of (not owned; may be NULL).
 * @param repr        Display text to copy; NULL takes the breakpoint's own default.
 * @param allocator   Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM.
 */
static inline dcg_breakpoint_node* c_dcg_node_new_breakpoint(dcg_logic_group* break_from, const char* repr, allocator_protocol* allocator) {
    dcg_breakpoint_node* node = (dcg_breakpoint_node*) c_ap_alloc(sizeof(dcg_breakpoint_node), allocator);
    if (!node) return NULL;

    if (c_dcg_node_init(&node->base, DCG_NODE_BREAKPOINT, repr ? repr : DCG_DEF_REPR_BREAKPOINT) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }
    node->base.eval_ctx.flags |= DCG_EVAL_FLAG_BREAKPOINT;

    /*
     * What entering a breakpoint means, armed here with everything else the type
     * is built with. The table in c_node.h cannot give it: its body needs the
     * breakpoint struct, and that is this header - one the table's header sits
     * below. A node is what its constructor makes it, so this is where the
     * answer belongs, and nothing has to correct the node afterwards.
     */
    node->base.ctx_ops.enter_fn = c_dcg_node_ctx_enter_breakpoint;

    node->base.autogen     = true;
    node->await_connection = false;
    node->break_from       = break_from;
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
 * @brief Take a breakpoint out of a graph, leaving its continuation in its place.
 *
 * A breakpoint is SCAFFOLDING: it stands where a branch broke so a build can
 * carry on somewhere else, and once the graph is built there is nothing left for
 * it to do. What the branch actually does is what the break resumed into, so the
 * bake splices that node into the breakpoint's place - and the arm the
 * breakpoint held becomes the continuation's, because where the branch goes did
 * not change when the scaffolding came down.
 *
 * This is where the "take over" belongs. Doing it while the graph is being BUILT
 * would mean a build could not name the break it made afterwards, and a node
 * that vanishes from under its own wrapper is a wrapper nobody can use; done
 * here, the graph the walk sees has no breakpoints in it and the wrapper still
 * owns a block that is simply no longer linked.
 *
 * The ELSE arm is the continuation, and it is the only arm a breakpoint has: one
 * child, the else the enter opened or the manager connected. The node is
 * detached rather than freed - the caller that built the breakpoint holds the
 * block, and freeing it here would leave that handle dangling.
 *
 * @param node  Breakpoint to resolve. Must be one, and must have resumed.
 * @return DCG_OK, DCG_ERR_TYPE (not a breakpoint), DCG_ERR_UNRESOLVED (it broke
 *         and never carried on - nothing to splice in) or the link's own code.
 */
static inline int c_dcg_node_resolve_breakpoint(dcg_node* node) {
    if (!node || node->ntype != DCG_NODE_BREAKPOINT) return DCG_ERR_TYPE;

    dcg_node* continuation = c_dcg_node_child_by_condition(node, DCG_ELSE_CONDITION);
    if (!continuation) return DCG_ERR_UNRESOLVED;

    /* Read off the breakpoint BEFORE the splice: the replace re-parents the
     * continuation and rewrites the edge it hangs by. */
    const dcg_node_edge_condition* inherited = node->condition_to_parent;

    /*
     * The continuation is let go of the breakpoint first, because a replace
     * refuses a node that still has a parent - its `new` is expected to be free
     * to link - and the breakpoint is its parent until this moment.
     *
     * `replace_shared` is the other door for a parented node, and it is the wrong
     * one here: it lands the node at the END of the new parent's list, which for
     * a breakpoint holding a branch's true arm would put the continuation after
     * the false arm and swap the two around. Detaching first keeps the slot: the
     * branch goes on meaning what it meant.
     */
    int ret = c_dcg_node_detach(continuation);
    if (ret != DCG_OK) return ret;

    ret = c_dcg_node_replace(node, continuation);
    if (ret != DCG_OK) return ret;

    c_dcg_node_adopt_condition(continuation, inherited);

    /* The breakpoint is out of the graph, and it has to stop pointing INTO it.
     * Detaching the continuation leaves the breakpoint's child list naming a
     * node that now belongs to another parent - a stale pointer that anything
     * walking the block afterwards follows, the block's own free included. */
    node->children = NULL;
    node->flags &= ~(uint32_t) DCG_NODE_FLAG_VISITED; /* out of the graph: the walks must not reach it */
    return DCG_OK;
}

/**
 * @brief Enter a breakpoint: open the one arm a build inside it fills.
 *
 * Entering is how a build goes INSIDE a breakpoint and carries the branch on
 * from there - what it builds becomes the node the breakpoint resumed into. The
 * breakpoint itself is not discarded: it stays on its arm until the bake takes
 * it down (c_dcg_node_resolve_breakpoint), and a build that wants to name the
 * break it made can go on doing so.
 *
 * The arm is ONE, and its condition is the ELSE. A breakpoint stands for "the
 * branch carried on here", which is what an else says: not a value the parent's
 * result is compared against, but the path taken where the branch that broke had
 * nothing left to decide.
 *
 * There are TWO ways a breakpoint resumes, and this call settles which one is in
 * play. The other is the manager's, and it is the manager's to give up: a queued
 * breakpoint is connected to the next node entered anywhere, and a breakpoint
 * cannot resume into itself - see c_dcg_lgm_connect_awaiting, which drops the
 * breakpoint being entered as it passes. What is left here is the arm, which is
 * the node's own business and needs nothing the node does not carry.
 *
 * Entering a breakpoint that already resumed into something is refused rather
 * than given a second arm: a breakpoint resumes into exactly one node.
 *
 * @param node  Breakpoint being entered.
 * @param mgr   Manager holding the build (unused: opening an arm is the node's
 *              own; the queue half is settled where the queue is).
 * @return DCG_OK, DCG_ERR_INVALID_ARG, DCG_ERR_TYPE (not a breakpoint),
 *         DCG_ERR_BUSY (it already resumes into something) or DCG_ERR_OOM.
 */
static inline int c_dcg_node_ctx_enter_breakpoint(dcg_node* node, dcg_logic_group_manager* mgr) {
    (void) mgr;

    if (!node) return DCG_ERR_INVALID_ARG;
    if (node->ntype != DCG_NODE_BREAKPOINT) return DCG_ERR_TYPE;
    if (node->children) return DCG_ERR_BUSY;

    /* The build is inside it now, so it is not waiting for anything: the flag is
     * a fact about the node, which is why it is cleared here rather than where
     * the queue is. The QUEUE half is the manager's, and the manager drops it as
     * it passes - see c_dcg_lgm_connect_awaiting. */
    ((dcg_breakpoint_node*) node)->await_connection = false;

    dcg_node* arm = c_dcg_node_new_placeholder(c_ap_protocol_from_ptr(node));
    if (!arm) return DCG_ERR_OOM;

    int ret = c_dcg_node_append(node, arm, DCG_ELSE_CONDITION);
    if (ret != DCG_OK) {
        c_dcg_node_free(arm);
        return ret;
    }
    return DCG_OK;
}

/**
 * @brief The first breakpoint below a node that has not resumed into anything.
 *
 * What a caller is asking for is the break a build left behind: the place the
 * graph stopped, which another build can take up and carry on from. That is a
 * breakpoint with no child - one that has resumed into something is where a
 * branch CONTINUED, not where it stopped - and it is the same test the capi's
 * ``get_breakpoint`` makes by looking for a breakpoint among the leaves.
 *
 * The walk does NOT descend into a breakpoint, and that is load-bearing rather
 * than tidy: a resumed node stands in two parents' lists at once (see the join
 * in c_dcg_node_append), so a breakpoint's child list is BORROWED and its
 * sibling chain runs on into the other parent's children. Walking there would
 * leave the tree and could come back round to a node already visited. A waiting
 * breakpoint has no such list, so stopping at every breakpoint keeps the walk
 * inside a real tree - each node reached once, from its own parent.
 *
 * @param node  Node to search below (NULL-safe).
 * @return The waiting breakpoint, or NULL when there is none.
 */
static inline dcg_breakpoint_node* c_dcg_node_get_breakpoint(const dcg_node* node) {
    if (!node) return NULL;

    for (const dcg_node* child = node->children; child; child = child->next_sibling) {
        if (child->ntype == DCG_NODE_BREAKPOINT) {
            if (!child->children) return (dcg_breakpoint_node*) child;
            continue; /* resumed: its child list is borrowed, not ours to walk */
        }
        dcg_breakpoint_node* found = c_dcg_node_get_breakpoint(child);
        if (found) return found;
    }
    return NULL;
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
 * @brief Free a node whose type is only known at run time.
 *
 * c_dcg_node_free() releases the base half of a node; a family that puts state
 * after the base header releases that state in its own _free. This is the one
 * place that knows both: the node's own `ntype` is the dispatch flag, and the
 * free of the type it names is the one called - a literal and a variable
 * through their two input frees, the operator family through
 * c_dcg_node_free_expr(), the whole action family through
 * c_dcg_node_free_action(), a root and a breakpoint through theirs.
 *
 * A family whose types have different layouts is split here, not inside its own
 * free: a variable is not a literal, and a free that took one as the other
 * would be reading a block that is not that shape.
 *
 * A type this dispatcher has no free for - a variant it has never heard of -
 * is reported on stderr, and its base is released all the same: the silent
 * alternative would hand back half a node and leak whatever the type put
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

    /* Every type with a free of its own returned above, so getting here means
     * the type is one this dispatcher cannot take apart. Say so: a node like
     * that is a bug in whoever built it, and the message is the only trace the
     * caller gets. */
    (void) fprintf(stderr, "c_dcg_node_free_generic: no free for node type %s (0x%04x) at %p - releasing the base only\n", c_dcg_node_type_name(node->ntype), (unsigned) node->ntype, (const void*) node);
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

    c_dcg_node_invoke_callbacks(node, DCG_NODE_EVENT_CHILD_CLEARED, node, (uint64_t) -1);
    return freed;
}

/**
 * @brief Back to a fresh state, keeping the node's identity and bindings.
 *
 * Drops the subtree and the labels, resets the value slot and the eval
 * scratch, but KEEPS: the type, the operator, the repr and its ownership
 * flag, the uid, the eval hooks, the mutation callbacks and the
 * user_payload. This is what a builder calls to reuse a node block.
 *
 * An action's slot is the one value the reset puts BACK, because it is not a
 * result the node produced: it is what the node IS.
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

    /* An action's slot is the node itself, written when it was built rather than
     * produced by any evaluation - so a clean that empties the slot has to put it
     * back, or the node stops being its own value for good. */
    if (c_dcg_node_type_is_action(node->ntype)) (void) c_dcg_var_init_node(&node->out, node);

    node->eval_ctx.run    = NULL;
    node->eval_ctx.flags  = DCG_EVAL_FLAG_NONE;
    node->eval_ctx.depth  = 0;
    node->eval_ctx.visits = 0;

    if (ret >= 0 && !had_children) c_dcg_node_invoke_callbacks(node, DCG_NODE_EVENT_CHILD_CLEARED, node, (uint64_t) -1);
    return ret;
}

/*
 * The expression family, and through it the protocol - LAST, because the two are
 * a cycle with this header: this one frees the family's nodes by type, and the
 * protocol's walk needs the root and the path record this header defines, both
 * as COMPLETE types. Including either one up top would parse it while this
 * header's own structs are still unread, which is the one order that cannot
 * work. Down here they are all declared, and the header is finished but for this
 * line, so the cycle closes with every type already known.
 */
#include <decision_graph/decision_tree/bake/c_expr.h>

#endif  // C_DCG_BAKE_HIERARCHY_H
