#ifndef C_DCG_BAKE_EVAL_H
#define C_DCG_BAKE_EVAL_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cbase/allocator_protocol/c_allocator_protocol.h>

#include <decision_graph/decision_tree/bake/c_const.h>
#include <decision_graph/decision_tree/bake/c_edge.h>
#include <decision_graph/decision_tree/bake/c_expr.h>
#include <decision_graph/decision_tree/bake/c_hierarchy.h>
#include <decision_graph/decision_tree/bake/c_node.h>
#include <decision_graph/decision_tree/bake/c_var.h>

/*
 * THE EVALUATION PROTOCOL - the half of the layer that makes a graph decide.
 *
 * A build says what the graph IS; an evaluation says what it DOES. The walk
 * goes from the top down, updating one node's out slot at a time, and comes to
 * rest on a single leaf of the whole tree. The nodes off that path keep the
 * values they had: nothing is evaluated that the walk did not reach.
 *
 * ONE NODE, in order - the three hooks a node carries, around the value it
 * produces:
 *
 *   1. pre_eval_fn, if the node has one. It may fail, and a failure stops the
 *      node where it stands: what it refused to set up is not set up.
 *   2. eval_fn - the node's own way of producing a value, if a caller installed
 *      one. Without one the value comes from the node's TYPE, and for an
 *      operator node from its OP CODE as well: an operator node is given the
 *      rule of its own operator when the operator is set (see c_expr.h), and
 *      that rule IS its type_eval_fn - so the value is reached through the rule
 *      the node carries, either at bake time (DCG_EVAL_DIRECT_HOOKS, which
 *      installs a family's rule as the node is built) or by the dispatch that
 *      finds it from the type (c_dcg_node_eval_default) - the same functions
 *      either way. A type with no rule of its own - a call, whose callee is
 *      composed into display text and stored nowhere - reports DCG_ERR_TYPE
 *      rather than producing a value nothing can read.
 *   3. post_eval_fn, if the node has one. By here the slot holds the value, so
 *      a hook that runs at this point reacts to it rather than producing it.
 *
 * What the protocol does NOT do is hide a hook's failure: a hook that raises
 * comes back as DCG_ERR_HOOK, and everything after it in the node is skipped.
 * A hook is a function pointer, so the layer never calls one that was never
 * installed - there is no dummy to dispatch on.
 *
 * THE BOOKKEEPING, on the node's own context, once the three stages are done:
 * which stages completed and WHAT produced the value (dcg_eval_stage bits, the
 * producer among them), what the node ended with (err_code), the run that wrote
 * the value (eval_seq_id), how deep the node sat (depth) and how many times it
 * has been evaluated (visits). The node is then
 * announced with DCG_NODE_EVENT_EVALUATED, carrying the run's id - so a
 * listener hears which run produced the value it is looking at.
 *
 * THE WALK, after the node's own value stands:
 *
 *   4. the value selects the edge. The node's children are tried in the order
 *      they are stored - which is the order they are read, the true arm before
 *      its fallback - and the first whose condition MATCHES the value is taken.
 *      An ELSE arm is held back and taken only when nothing else matched, so a
 *      fallback cannot shadow a branch written after it (see
 *      c_dcg_condition_matches).
 *   5. the walk descends into that child, one level deeper, and starts again at
 *      step 1. A node with no children is a leaf and ends the walk; a node
 *      whose children all declined is DCG_ERR_NO_MATCH.
 *
 * A node carrying DCG_EVAL_FLAG_SKIP_CHILDREN ends the walk where it stands, and
 * one carrying DCG_EVAL_FLAG_BREAKPOINT is an inspection point: recorded, and
 * passed through to what it is connected to when there is anything below it. A
 * node carrying DCG_EVAL_FLAG_CACHED keeps the value it already holds - its eval
 * hook is not called - which is how a caller reuses a value it computed itself.
 *
 * THE FOUR WAYS IN, in the order they are built on each other:
 *
 *   - c_dcg_node_eval(): ONE node, and nothing else. No bookkeeping is written
 *     and no child is reached, so the value it produces stays in the node's slot.
 *   - c_dcg_node_dryrun(): the same, asked as a question - the value goes to a
 *     slot the caller lends and the node is left exactly as it was found.
 *   - c_dcg_node_eval_graph(): a walk from wherever it is given, recording the
 *     nodes it reached into a path the caller lends it.
 *   - c_dcg_root_node_eval(): the same walk from a root, recorded into the root's
 *     OWN eval path - the record a caller reads back to see how the decision was
 *     reached.
 *
 * The header is C only: it is above every family (it includes them) and nothing
 * includes it. A Cython module that binds it does so with a textual
 * `cdef extern from`, so it creates no pxd edge and no cycle - see DEPENDENCY.md
 * section 3.2.
 */

/** Entries the eval path starts with; it doubles as the walk grows it. */
#ifndef DCG_EVAL_PATH_INITIAL_CAPACITY
#define DCG_EVAL_PATH_INITIAL_CAPACITY 16U
#endif

// ========== Constants ==========

/* The stage mask a node records its evaluation in - and the producer bits that
 * ride in it - are declared in c_node.h, where the context that carries them is:
 * this header is above that one and fills the mask (see c_dcg_node_eval_hooks). */

// ========== Structs ==========

/**
 * @brief The state of one walk - what every node of a run shares.
 *
 * It is what the evaluator hangs off `dcg_node_eval_ctx.run` while a node is
 * being evaluated, so a hook can ask about the run it is part of rather than
 * only about its own node: what has been reached so far, how deep the walk is,
 * and whether anything has failed. It lives on the walker's stack for the
 * length of the walk, and no node keeps a pointer to it beyond its own visit.
 */
typedef struct dcg_eval_run {
    uint64_t     seq_id;   // The run's id, stamped on every node it visits.
    dcg_ret_code code;     // DCG_OK so far, or the first failure.
    dcg_node*    failed;   // The node that reported that failure.
    dcg_node*    leaf;     // The node the walk came to rest on.
    size_t       visited;  // Nodes whose evaluation completed.
    size_t       depth;    // The deepest level the walk reached.
    bool         inplace;  // Whether a value produced under it is kept.
} dcg_eval_run;

// ========== Forward Declarations ==========

// The path record
static inline dcg_node_eval_path* c_dcg_node_eval_path_new(size_t capacity, allocator_protocol* allocator);
static inline void                c_dcg_node_eval_path_free(dcg_node_eval_path* path);
static inline void                c_dcg_eval_path_reset(dcg_node_eval_path* path);
static inline int                 c_dcg_eval_path_append(dcg_node_eval_path* path, dcg_node* node);
static inline int                 c_dcg_root_node_eval_path_append(dcg_root_node* root, dcg_node* node);

// The built-in evaluation
static inline int                 c_dcg_node_eval_default(dcg_node* node);

// The node's own evaluation
static inline int                 c_dcg_node_eval_hooks(dcg_node* node);
static inline int                 c_dcg_node_eval(dcg_node* node);
static inline int                 c_dcg_node_dryrun(dcg_node* node, dcg_var_t* out);

// The walk
static inline uint64_t            c_dcg_eval_gen_seq_id(const dcg_node* node);
static inline dcg_node*           c_dcg_eval_select_child(const dcg_node* node);
static inline int                 c_dcg_node_eval_visit(dcg_node* node, dcg_eval_run* run, size_t depth, dcg_root_node* root, dcg_node_eval_path* path);
static inline void                c_dcg_eval_path_outcome(dcg_root_node* root, dcg_node_eval_path* path, const dcg_eval_run* run, dcg_node* leaf, dcg_node* failed);
static inline int                 c_dcg_eval_walk(dcg_node* node, dcg_root_node* root, dcg_node_eval_path* path);
static inline int                 c_dcg_node_eval_graph(dcg_node* node, dcg_node_eval_path* path);
static inline int                 c_dcg_root_node_eval(dcg_root_node* root);

// ========== The Path Record ==========

/**
 * @brief Empty a path record, keeping the room it has.
 *
 * The entries go, the two blocks and the room stay: a caller that evaluates
 * repeatedly reuses the record rather than allocating for every walk, and a path
 * is emptied before every walk because what it holds belongs to the last one.
 *
 * @param path  Path to empty (NULL-safe).
 */
static inline void                c_dcg_eval_path_reset(dcg_node_eval_path* path) {
    if (!path) return;
    path->n_nodes = 0;
    path->code    = DCG_OK;
    path->leaf    = NULL;
    path->failed  = NULL;
    path->seq_id  = 0;
}

/**
 * @brief Allocate a record of its own, with room for `capacity` entries.
 *
 * The way to record a walk that is not a root's: the record is one block, the
 * two arrays are blocks nested under it, and c_dcg_node_eval_path_free()
 * releases all three in one walk - so a record can be a local, a field of a
 * caller's own struct, or a piece of a larger one, and it never belongs to the
 * graph it describes.
 *
 * What to ask for is what the walk can need, and that is knowable before it
 * runs: a walk visits one node per LEVEL and never two at the same level, so
 * while it may stop earlier, it cannot go deeper than the graph does -
 * c_dcg_node_height(node) + 1 is the most entries any walk from `node` can use.
 * A graph that grows between walks can outgrow the room; the record then keeps
 * its END and counts what it lost (see c_dcg_eval_path_append).
 *
 * @param capacity   Entries to make room for; 0 takes the default.
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The record, or NULL on OOM.
 */
static inline dcg_node_eval_path* c_dcg_node_eval_path_new(size_t capacity, allocator_protocol* allocator) {
    if (capacity == 0) capacity = DCG_EVAL_PATH_INITIAL_CAPACITY;

    dcg_node_eval_path* path = (dcg_node_eval_path*) c_ap_alloc(sizeof(dcg_node_eval_path), allocator);
    if (!path) return NULL;

    path->capacity = capacity;
    path->node     = (dcg_node**) c_ap_alloc_child(capacity * sizeof(dcg_node*), allocator, path);
    path->eval_val = (dcg_var_t*) c_ap_alloc_child(capacity * sizeof(dcg_var_t), allocator, path);
    if (!path->node || !path->eval_val) {
        c_ap_free_owned(path); /* the arrays are its children: one free releases whatever was made */
        return NULL;
    }

    c_dcg_eval_path_reset(path);
    return path;
}

/**
 * @brief Release a record of its own, and its arrays with it.
 *
 * Only for a record from c_dcg_node_eval_path_new(). The root's embedded record
 * is not freed - it is not a block, it is a field of the root, and the root's
 * own free releases what it holds.
 *
 * @param path  Record to free (NULL-safe).
 */
static inline void c_dcg_node_eval_path_free(dcg_node_eval_path* path) {
    if (!path) return;
    c_ap_free_owned(path);
}

/**
 * @brief Record one visited node and the value it stood for.
 *
 * The value is what the node's slot READS AS, not a copy of the slot itself: a
 * read's slot is a live reference, and a record that stored the reference would
 * report whatever the store holds at the moment it is read back rather than what
 * the node evaluated to (see c_dcg_var_snapshot). The snapshot is shallow - a
 * payload the node owns is borrowed, not copied - so it is valid for as long as
 * the node is not evaluated again.
 *
 * A record GROWS: a walk as deep as the graph can make one still records every
 * node of it, because a record that silently lost its first entries would be a
 * record of a different walk. Room is the caller's to size (a walk can never
 * visit more than c_dcg_node_height(node) + 1 nodes, which is what
 * c_dcg_node_eval_path_new() and the root's own record ask for), and a record
 * that needs more simply takes it.
 *
 * @param path  Record to add to (must have room made for it).
 * @param node  Node that was visited.
 * @return DCG_OK, DCG_ERR_INVALID_ARG (NULL path or node, or a record that was
 *         never given room), or DCG_ERR_OOM.
 */
static inline int c_dcg_eval_path_append(dcg_node_eval_path* path, dcg_node* node) {
    if (!path || !node) return DCG_ERR_INVALID_ARG;
    if (!path->node || !path->eval_val) return DCG_ERR_INVALID_ARG;

    if (path->n_nodes == path->capacity) {
        size_t     grown = path->capacity ? path->capacity * 2 : DCG_EVAL_PATH_INITIAL_CAPACITY;

        /* Each array is grown with the allocator read from the very block being
         * grown, and it has to be that way: a realloc MOVES a block - it takes a
         * new one and frees the old - so an allocator read from the first array
         * is into freed memory by the time the second one is grown. The block a
         * call is about to grow is alive at the call, and realloc reads the
         * allocator before it frees anything. */
        dcg_node** grown_nodes = (dcg_node**) c_ap_realloc(path->node, grown * sizeof(dcg_node*), c_ap_protocol_from_ptr(path->node));
        if (!grown_nodes) return DCG_ERR_OOM;
        path->node = grown_nodes;

        dcg_var_t* grown_values = (dcg_var_t*) c_ap_realloc(path->eval_val, grown * sizeof(dcg_var_t), c_ap_protocol_from_ptr(path->eval_val));
        if (!grown_values) return DCG_ERR_OOM;
        path->eval_val = grown_values;

        path->capacity = grown;
    }

    path->node[path->n_nodes] = node;
    c_dcg_var_snapshot(&path->eval_val[path->n_nodes], &node->out);
    path->n_nodes++;
    return DCG_OK;
}

/**
 * @brief Record one visited node into a root's own record - its blocks, its room.
 *
 * The variant a root's walk appends through, and the reason it exists: a root's
 * record is EMBEDDED in the root, not a block of its own, so its arrays are
 * nested under the root and released with it. The room is made once, the first
 * time a walk records anything, and sized for what a walk can need - the height
 * of the graph, plus the node the walk starts from.
 *
 * @param root  Root whose record to add to.
 * @param node  Node that was visited.
 * @return DCG_OK, DCG_ERR_INVALID_ARG, or DCG_ERR_OOM when the room could not be
 *         made.
 */
static inline int c_dcg_root_node_eval_path_append(dcg_root_node* root, dcg_node* node) {
    if (!root || !node) return DCG_ERR_INVALID_ARG;

    dcg_node_eval_path* path = &root->eval_path;
    if (!path->node) {
        allocator_protocol* allocator = c_ap_protocol_from_ptr(root);
        size_t              capacity  = c_dcg_node_height(&root->base) + 1; /* one node per level, at most */

        path->node     = (dcg_node**) c_ap_alloc_child(capacity * sizeof(dcg_node*), allocator, root);
        path->eval_val = (dcg_var_t*) c_ap_alloc_child(capacity * sizeof(dcg_var_t), allocator, root);
        if (!path->node || !path->eval_val) {
            if (path->node) c_ap_free_owned(path->node); /* the root outlives this: release what was made */
            path->node     = NULL;
            path->eval_val = NULL;
            return DCG_ERR_OOM;
        }
        path->capacity = capacity;
    }

    return c_dcg_eval_path_append(path, node);
}

// ========== The Built-in Evaluation ==========

/**
 * @brief The built-in evaluation of a node: the rule its type gives it.
 *
 * Most of the layer needs no evaluation at all, because what a node is worth is
 * settled when the node is BUILT - and saying so here is what keeps an
 * evaluation cheap:
 *
 *   - a LITERAL's slot is its value, written when it was built;
 *   - a READ's slot is a reference to its store entry - or, before the read has
 *     ever been evaluated, the entry's OFFSET, which the evaluation spends
 *     (c_dcg_node_mapping_var_node_eval_hook);
 *   - an ACTION's slot is the node itself, written at birth;
 *   - a ROOT's slot is true and a BREAKPOINT passes through, both as they were
 *     built.
 *
 * So there is exactly ONE rule left to run: an OPERATOR node's, which produces
 * its own operands and applies its operator to them (see c_expr.h).
 *
 * This function is that dispatch, and it is what a node built by a TU with
 * DCG_EVAL_DIRECT_HOOKS off reaches at every evaluation; with it on, the rules
 * a node does have are installed as its own hook at bake time and this is not
 * reached for them. Either way the rule that runs is the same function: for an
 * operator node the two ways in are its own rule ON the node (injected) and
 * c_dcg_node_expr_eval (found from the operator), and both land on the same one.
 *
 * @param node  Node to evaluate.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_eval_default(dcg_node* node) {
    if (!node) return DCG_ERR_INVALID_ARG;

    switch ((int) node->ntype & DCG_NODE_FAMILY_MASK) {
        case DCG_NODE_INPUT: {
            /* A literal is its value and a read is a reference to its entry: only
             * the read has anything to say at run time, and only the read with no
             * store to ask (a store's read brings its own rule). */
            if (node->ntype != DCG_NODE_VARIABLE) return DCG_OK;

            /* A read a STORE made never arrives here: it is built with the store's
             * own rule on it (see c_collections.h). What is left is a read of no
             * store - one bound by hand to another node's slot - and it has only
             * that slot to answer with: it either says what it reflects, or it
             * says nothing has landed behind it yet, or it is not a reference at
             * all and the node is malformed. */
            dcg_variable_node* var = (dcg_variable_node*) node;
            dcg_var_t*         out = &var->base.out;
            if (!c_dcg_var_is_ref(out->dtype)) return c_dcg_var_is_null(out) ? DCG_ERR_UNBOUND : DCG_ERR_TYPE;
            return c_dcg_var_ref_base(out->dtype) == VAR_TYPE_RESERVED ? DCG_ERR_UNBOUND : DCG_OK;
        }

        case DCG_NODE_OP:
            /* The family's own entry, which finds the rule from the node's
             * operator and its arity (c_expr.h). A build that injects its rules
             * (DCG_EVAL_DIRECT_HOOKS) never arrives here - its operator nodes
             * carry theirs - and both ways land on the same rule. */
            return c_dcg_node_expr_eval(node, NULL);

        case DCG_NODE_ACTION:
        case DCG_NODE_SPECIAL:
            /* Settled at birth: an action is its own value, a root is true, a
             * breakpoint passes through. */
            return DCG_OK;

        default:
            break;
    }

    (void) fprintf(stderr, "c_dcg_node_eval_default: no evaluation for node type %s (0x%04x) at %p\n", c_dcg_node_type_name(node->ntype), (unsigned) node->ntype, (const void*) node);
    return DCG_ERR_TYPE;
}

// ========== The Node's Own Evaluation ==========

/**
 * @brief Run a node's three stages, recording on the node how far they got.
 *
 * This is the protocol body: the pre hook, then the eval hook or the built-in
 * rule, then the post hook, each one skipped when the node carries no hook of
 * that kind. What it completed - and which of the three producers got the value
 * there - is written onto the node's own context as it goes, because that record
 * is about the node and belongs on it: a caller asking why a node failed reads
 * the stage it stopped at off the node, not off a return it had to keep.
 *
 * @param node  Node to evaluate.
 * @return DCG_OK, or the code that stopped it.
 */
static inline int c_dcg_node_eval_hooks(dcg_node* node) {
    if (!node) return DCG_ERR_INVALID_ARG;

    dcg_node_eval_ctx* ctx  = &node->eval_ctx;
    uint32_t           done = DCG_EVAL_STAGE_NONE; /* the mask as it is built, before it is recorded */
    int                ret_code;

    /* The record of this evaluation starts empty, and is written onto the node as
     * it progresses rather than handed back to a caller: what ran, and what
     * produced the value, is state about the node - and a failure leaves the
     * stage it stopped at where anyone asking about the node can read it. */
    ctx->stage = DCG_EVAL_STAGE_NONE;

    if (ctx->pre_eval_fn) {
        ret_code = ctx->pre_eval_fn(node, ctx->user_data);
        if (ctx->flags & DCG_EVAL_FLAG_TRACE) {
            (void) fprintf(stderr, "[eval] pre  %s (0x%04x) %p -> %s\n", c_dcg_node_type_name(node->ntype), (unsigned) node->ntype, (const void*) node, c_dcg_ret_code_name(ret_code));
        }
        if (ret_code != DCG_OK) {
            ctx->stage = done;
            return ret_code;
        }
    }
    done |= DCG_EVAL_STAGE_PRE_EVAL;

    if (ctx->flags & DCG_EVAL_FLAG_CACHED) {
        /* The node holds a value a caller computed itself; the slot stands as
         * it is and the eval hook is not called. */
    }
    else if (ctx->eval_fn) {
        done |= DCG_EVAL_STAGE_HOOK;
        ret_code = ctx->eval_fn(node, ctx->user_data);
        if (ctx->flags & DCG_EVAL_FLAG_TRACE) {
            (void) fprintf(stderr, "[eval] eval %s (0x%04x) %p -> %s\n", c_dcg_node_type_name(node->ntype), (unsigned) node->ntype, (const void*) node, c_dcg_ret_code_name(ret_code));
        }
        if (ret_code != DCG_OK) {
            ctx->stage = done;
            return ret_code;
        }
    }
    else if (ctx->type_eval_fn) {
        /* The rule the node's type gave it, installed as the node was built: a
         * caller's hook is an override of it, so this is only reached when there
         * is none (see DCG_EVAL_DIRECT_HOOKS). */
        done |= DCG_EVAL_STAGE_TYPE_RULE;
        ret_code = ctx->type_eval_fn(node, NULL);
        if (ctx->flags & DCG_EVAL_FLAG_TRACE) {
            (void) fprintf(stderr, "[eval] type %s (0x%04x) %p -> %s\n", c_dcg_node_type_name(node->ntype), (unsigned) node->ntype, (const void*) node, c_dcg_ret_code_name(ret_code));
        }
        if (ret_code != DCG_OK) {
            ctx->stage = done;
            return ret_code;
        }
    }
    else {
        done |= DCG_EVAL_STAGE_BUILTIN;
        ret_code = c_dcg_node_eval_default(node);
        if (ctx->flags & DCG_EVAL_FLAG_TRACE) {
            (void) fprintf(stderr, "[eval] def  %s (0x%04x) %p -> %s\n", c_dcg_node_type_name(node->ntype), (unsigned) node->ntype, (const void*) node, c_dcg_ret_code_name(ret_code));
        }
        if (ret_code != DCG_OK) {
            ctx->stage = done;
            return ret_code;
        }
    }
    done |= DCG_EVAL_STAGE_EVAL;

    if (ctx->post_eval_fn) {
        ret_code = ctx->post_eval_fn(node, ctx->user_data);
        if (ctx->flags & DCG_EVAL_FLAG_TRACE) {
            (void) fprintf(stderr, "[eval] post %s (0x%04x) %p -> %s\n", c_dcg_node_type_name(node->ntype), (unsigned) node->ntype, (const void*) node, c_dcg_ret_code_name(ret_code));
        }
        if (ret_code != DCG_OK) {
            ctx->stage = done;
            return ret_code;
        }
    }
    done |= DCG_EVAL_STAGE_POST_EVAL | DCG_EVAL_STAGE_DONE;

    ctx->stage = done;
    return DCG_OK;
}

/**
 * @brief Evaluate one node and nothing else, keeping the value it produces.
 *
 * No child is reached and nothing about a RUN is written: the node's sequence
 * id, depth and visit count are left exactly as they were, so one node can be
 * evaluated between two walks without disturbing them. What the node IS asked
 * for is its hooks, so a hook that has side effects has them either way - and
 * what the evaluation leaves behind is its own outcome, the stage it failed at
 * and the code it ended with, because those are about this evaluation and a
 * caller told "it failed" is owed the stage it failed at.
 *
 * The run pointer is lent to the node for the duration and given back
 * afterwards, so a hook reading `eval_ctx.run` sees a run - a minimal one, with
 * no id and no walk behind it - instead of whatever the last real walk left
 * there.
 *
 * @param node  Node to evaluate.
 * @return DCG_OK, or the code that stopped it.
 */
static inline int c_dcg_node_eval(dcg_node* node) {
    if (!node) return DCG_ERR_INVALID_ARG;

    dcg_eval_run lent;
    memset(&lent, 0, sizeof(lent));
    lent.inplace = true;

    dcg_node_eval_ctx* ctx          = &node->eval_ctx;
    void*              previous_run = ctx->run;

    ctx->run     = &lent;
    int ret_code = c_dcg_node_eval_hooks(node);
    ctx->run     = previous_run;

    /* What the node's OWN evaluation did is written onto it by the protocol
     * itself - the stage it got through and the code it ended with, stage
     * included - because those are about this evaluation, and a caller that is
     * told "it failed" is owed the stage it failed at. The state that belongs to
     * a RUN stays as it was: this is not one, so it neither counts a visit, nor
     * claims the depth, nor stamps a run id on the node. */
    ctx->err_code = ret_code;

    return ret_code;
}

/**
 * @brief Ask one node what it WOULD evaluate to - the value out, the node as it
 * was.
 *
 * Everything c_dcg_node_eval() does, and then the node is put back the way it
 * was found: what the evaluation produced goes to the caller's slot instead of
 * staying in the node's, and the node's own slot is restored - so a node a walk
 * has already given a value to can be asked again without that value being
 * disturbed, and a node under which a subtree is about to be walked can be
 * asked what it says first.
 *
 * The slot IS the answer, and the value moves into it whole: a payload the
 * produced value owns goes with it, and nothing is copied. A value the caller
 * does not take (`out` NULL) is released here instead, so a question never
 * leaks the answer to it. What the caller's slot must not be is a slot that
 * owns something: what it held is overwritten.
 *
 * The node's slot is then put back as it stood - the value that was there
 * before anything ran - which is safe because a rule writes OVER the slot it is
 * given (a kernel re-inits it) and never releases what it held.
 *
 * What is written onto the node is its own outcome, the stage it got through
 * and the code it ended with, exactly as c_dcg_node_eval() writes it; nothing
 * about a RUN is, since this is not one.
 *
 * @param node  Node to evaluate.
 * @param out   Receives the value the node produced (may be NULL: the value is
 *              then released and only the outcome is reported).
 * @return DCG_OK, or the code that stopped it.
 */
static inline int c_dcg_node_dryrun(dcg_node* node, dcg_var_t* out) {
    if (!node) return DCG_ERR_INVALID_ARG;

    dcg_eval_run lent;
    memset(&lent, 0, sizeof(lent));
    lent.inplace = false; /* the value leaves the node instead of staying in it */

    dcg_node_eval_ctx* ctx            = &node->eval_ctx;
    void*              previous_run   = ctx->run;
    dcg_var_t          previous_value = node->out;

    ctx->run     = &lent;
    int ret_code = c_dcg_node_eval_hooks(node);
    ctx->run     = previous_run;

    ctx->err_code = ret_code;

    if (out) *out = node->out; /* the answer goes to the caller ... */
    else c_dcg_var_dealloc(&node->out);

    node->out = previous_value; /* ... and the node keeps what it came in with */
    return ret_code;
}

// ========== The Walk ==========

/**
 * @brief Mint the id of a run.
 *
 * The id is what a node's `eval_seq_id` carries afterwards and what the
 * EVALUATED event is announced with, so a listener can tell whose value it is
 * looking at. It is derived from the top node's identity and a counter, which
 * makes it distinct between the runs of one process - the pid is part of the
 * mix, but two processes are not expected to compare their ids.
 *
 * @param node  Top of the walk (may be NULL: the id is then just the counter).
 * @return The run's id.
 */
static inline uint64_t c_dcg_eval_gen_seq_id(const dcg_node* node) {
    static uint64_t counter = 0;
    counter++;
    return c_dcg_node_gen_seq_id(node) ^ (counter * 0x9E3779B97F4A7C15ULL);
}

/**
 * @brief Which child the node's value selects - the edge the walk follows.
 *
 * The children are tried in stored order, which is the order the arms were
 * written and read: the true arm before its fallback. An ELSE arm is held back
 * and taken only when nothing else matched, so a fallback does not shadow a
 * branch that was written after it.
 *
 * @param node  Node whose value selects the edge.
 * @return The child to descend into, or NULL when none was selected.
 */
static inline dcg_node* c_dcg_eval_select_child(const dcg_node* node) {
    if (!node) return NULL;

    dcg_node* fallback = NULL;
    for (dcg_node* child = node->children; child; child = child->next_sibling) {
        if (c_dcg_condition_is_else(child->condition_to_parent)) {
            if (!fallback) fallback = child;
            continue;
        }
        if (c_dcg_condition_matches(child->condition_to_parent, &node->out)) return child;
    }
    return fallback;
}

/**
 * @brief Evaluate one node as part of a walk: the protocol and its bookkeeping.
 *
 * The three stages run (c_dcg_node_eval_hooks), and then what happened is
 * written onto the node: which stages completed, what it ended with, the run it
 * belongs to, how deep it sat and that it has been visited once more. The node
 * is announced afterwards with DCG_NODE_EVENT_EVALUATED - carrying the run's id
 * as the event's sequence id - but only when its evaluation completed, because
 * an event named "evaluated" about a node that produced no value is a lie a
 * listener would act on. A node that failed is left with the code in its
 * context, which is what a listener asking about it can read.
 *
 * The node is recorded in the path when one is lent, whether the evaluation
 * succeeded or not: the record of how a decision was reached has to be able to
 * show where it stopped being reached.
 *
 * @param node   Node to evaluate.
 * @param run    The walk's state.
 * @param depth  How deep the node sits.
 * @param root   The root a walk started from, when it started from one - the
 *               record is then the root's own (may be NULL).
 * @param path   Record to add the node to, when it is not a root's (may be NULL).
 * @return DCG_OK, or the code that stopped the node.
 */
static inline int c_dcg_node_eval_visit(dcg_node* node, dcg_eval_run* run, size_t depth, dcg_root_node* root, dcg_node_eval_path* path) {
    if (!node || !run) return DCG_ERR_INVALID_ARG;

    dcg_node_eval_ctx* ctx          = &node->eval_ctx;
    void*              previous_run = ctx->run;

    ctx->run = run;
    int      ret_code = c_dcg_node_eval_hooks(node);
    ctx->run          = previous_run;

    ctx->err_code    = ret_code;
    ctx->eval_seq_id = run->seq_id;
    ctx->depth       = depth;
    ctx->visits++;

    if (root) (void) c_dcg_root_node_eval_path_append(root, node);
    else if (path) (void) c_dcg_eval_path_append(path, node);

    if (ret_code == DCG_OK) {
        run->visited++;
        if (depth > run->depth) run->depth = depth;
        c_dcg_node_invoke_callbacks(node, DCG_NODE_EVENT_EVALUATED, node, run->seq_id);
    }
    else if (run->code == DCG_OK) {
        run->code   = ret_code;
        run->failed = node;
    }

    return ret_code;
}

/**
 * @brief The outcome of a walk, written where the record lives.
 *
 * A walk reports through two records at most - the root's own and the one a
 * caller lent - and they are written with the same code so neither can be the
 * one that forgot to say what happened.
 *
 * @param root   Root whose record to write (may be NULL).
 * @param path   Record to write (may be NULL).
 * @param run    The walk's state.
 * @param leaf   The node the walk came to rest on (NULL when it did not land).
 * @param failed The node that failed (NULL when none did).
 */
static inline void c_dcg_eval_path_outcome(dcg_root_node* root, dcg_node_eval_path* path, const dcg_eval_run* run, dcg_node* leaf, dcg_node* failed) {
    dcg_ret_code code   = failed ? run->code : DCG_OK;
    dcg_node*    landed = failed ? NULL : leaf;

    if (root) {
        root->eval_path.code   = code;
        root->eval_path.leaf   = landed;
        root->eval_path.failed = failed;
        root->eval_path.seq_id = run->seq_id;
    }
    if (path) {
        path->code   = code;
        path->leaf   = landed;
        path->failed = failed;
        path->seq_id = run->seq_id;
    }
}

/**
 * @brief Walk a graph from a node down to a leaf, evaluating as it goes.
 *
 * The walk is a loop, not a recursion: every step replaces the node with the
 * child its value selected, so a graph as deep as a build can make one costs no
 * stack. It stops at a leaf, at a node that asks not to be descended below
 * (DCG_EVAL_FLAG_SKIP_CHILDREN), at a breakpoint with nothing under it, or at
 * the first failure - and it reports where it stopped through `path` and through
 * the code it returns.
 *
 * The path is reset first: the record belongs to this walk and to no other. The
 * nodes visited are added to it in order, so the last entry is the node the walk
 * came to rest on. The outcome goes with them - `code`, `leaf`, `failed` and the
 * run's `seq_id` - and a walk that did not land leaves `leaf` NULL, so "which
 * node did it land on?" is never answered with the node it broke at.
 *
 * @param node  Node to start from - a root, or any node whose graph is to be
 *              evaluated from there down.
 * @param path  Record to fill (may be NULL). Its owner receives the blocks.
 * @return DCG_OK when a leaf was reached, or the DCG_ERR_* that stopped the walk
 *         (DCG_ERR_NO_MATCH included: a branching node whose value selected none
 *         of its children and which has no else arm).
 */
static inline int c_dcg_eval_walk(dcg_node* node, dcg_root_node* root, dcg_node_eval_path* path) {
    if (!node) return DCG_ERR_INVALID_ARG;

    /* A record with no room was never made, so a walk asked to fill one is
     * refused before it starts: walking and then reporting a value with no
     * record behind it is the one answer a caller asking for a record cannot
     * use. A walk asked for NO record runs all the same. */
    if (path && (!path->node || !path->eval_val)) return DCG_ERR_INVALID_ARG;

    if (root) c_dcg_eval_path_reset(&root->eval_path);
    if (path) c_dcg_eval_path_reset(path);

    dcg_eval_run run;
    memset(&run, 0, sizeof(run));
    run.seq_id  = c_dcg_eval_gen_seq_id(node);
    run.code    = DCG_OK;
    run.inplace = true;

    size_t    current_depth = 0;
    dcg_node* current       = node;

    while (current) {
        int ret_code = c_dcg_node_eval_visit(current, &run, current_depth, root, path);
        if (ret_code != DCG_OK) {
            c_dcg_eval_path_outcome(root, path, &run, current, current);
            return ret_code;
        }

        if (current->eval_ctx.flags & DCG_EVAL_FLAG_SKIP_CHILDREN) break;

        /* A node with no children is where the walk ends - a breakpoint that was
         * never connected included: there is nothing below it to resume into. */
        if (!current->children) break;

        dcg_node* child = c_dcg_eval_select_child(current);
        if (!child) {
            run.code = DCG_ERR_NO_MATCH;
            c_dcg_eval_path_outcome(root, path, &run, current, current);
            return DCG_ERR_NO_MATCH;
        }

        current = child;
        current_depth++;
    }

    run.leaf = current;
    c_dcg_eval_path_outcome(root, path, &run, current, NULL);
    return DCG_OK;
}

/**
 * @brief Walk a graph from a node down to a leaf, recording into a caller's path.
 *
 * @param node  Node to start from - any node whose graph is to be evaluated from
 *              there down.
 * @param path  Record to fill (may be NULL: a walk asked for no record runs all
 *              the same).
 * @return DCG_OK when a leaf was reached, or the DCG_ERR_* that stopped the walk
 *         (DCG_ERR_NO_MATCH included: a branching node whose value selected none
 *         of its children and which has no else arm).
 */
static inline int c_dcg_node_eval_graph(dcg_node* node, dcg_node_eval_path* path) {
    if (!node) return DCG_ERR_INVALID_ARG;
    return c_dcg_eval_walk(node, NULL, path);
}

/**
 * @brief Evaluate a graph from its root, recording into the root's own path.
 *
 * The root's eval path is the record of how the last walk reached its decision,
 * so it is emptied and refilled rather than lent to a caller: a root keeps the
 * one record of itself, and every walk replaces it. The record is the root's own
 * - embedded in it, its blocks nested under it - which is why the walk is given
 * the root and not the record.
 *
 * @param root  Root to evaluate.
 * @return DCG_OK, or the DCG_ERR_* that stopped the walk.
 */
static inline int c_dcg_root_node_eval(dcg_root_node* root) {
    if (!root) return DCG_ERR_INVALID_ARG;
    return c_dcg_eval_walk(&root->base, root, NULL);
}

#endif  // C_DCG_BAKE_EVAL_H
