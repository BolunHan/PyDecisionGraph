#ifndef C_DCG_BAKE_BAKE_H
#define C_DCG_BAKE_BAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cbase/allocator_protocol/c_allocator_protocol.h>

#include <decision_graph/decision_tree/bake/c_collections.h>
#include <decision_graph/decision_tree/bake/c_eval.h>
#include <decision_graph/decision_tree/bake/c_expr.h>
#include <decision_graph/decision_tree/bake/c_hierarchy.h>
#include <decision_graph/decision_tree/bake/c_node.h>
#include <decision_graph/decision_tree/bake/c_var.h>

/*
 * THE BAKE PROTOCOL - the pass that makes a built graph ready to be walked.
 *
 * A build says what a graph IS and an evaluation says what it DOES. Between the
 * two sits this: the last thing done to a graph before it is evaluated, and the
 * pass that an evaluation is written assuming has run. It is asked of the ROOT
 * and of nothing else, because a graph is entered from one place.
 *
 * THREE THINGS, and the order is the order they can be done in:
 *
 *   1. VERIFY. The shape an evaluation ASSUMES is established here and nowhere
 *      else - the hot path carries no check for a malformed node, because this
 *      is the check (see c_dcg_node_expr_eval_operand, and the rules above it).
 *      Two halves:
 *
 *        - the GRAPH's structure, which the node layer already knows how to
 *          check (c_dcg_node_validate: one parentless root, arms in reading
 *          order, an else last, an action below nothing, no cycles);
 *        - what an OPERATOR node needs before its rule can run at all, which is
 *          this header's own:
 *            . every operand its arity takes must have a component bound. The
 *              rule runs its operands and reads the workspace they filled, one
 *              slot at a time, with nothing checked in between: a slot with no
 *              component behind it is a read of nothing, not a value;
 *            . its operator must be one its own arity applies. An operator of
 *              another arity leaves the node carrying the rule that refuses to
 *              evaluate (c_dcg_node_expr_set_op), so the graph has a node that
 *              can never produce a value - refused here, before a walk
 *              discovers it one evaluation at a time;
 *            . its operand array must be exactly its arity long. A block sized
 *              for fewer operands than its rule reads is out of bounds, not
 *              merely wrong.
 *
 *      A CALL is refused by the same reading, and so is a node of the operator
 *      family's head type: neither has an evaluation at all (see
 *      c_dcg_node_expr_eval_call and c_dcg_node_expr_eval_unknown), and a graph
 *      holding one cannot be evaluated - a build defect, and this is the last
 *      moment it can be reported as one.
 *
 *   2. SEAL. What the pass verified, it then holds. Every node it walked is
 *      FROZEN, so the graph's structure is what it was: the node layer refuses
 *      an append, a detach, a removal and a clear under a frozen parent, and an
 *      expression refuses a rebind and an operator change under one too (see
 *      c_dcg_node_expr_bind and c_dcg_node_expr_set_op). And every STORE the
 *      graph reads - the group behind a read - is frozen with it, so no entry
 *      can appear in it afterwards.
 *
 *      The store is the half an evaluation depends on: a resolved read holds a
 *      reference INTO its store's block, and that block moves only when the
 *      store grows (see c_dcg_mapping_lgroup_get_node). A frozen store cannot
 *      grow, which is what keeps a resolved read's reference good.
 *
 *      What sealing does NOT stop is a VALUE. A store is sealed in SHAPE - the
 *      entries it has are the entries it will have - and a caller goes on
 *      writing what it already holds. That is the whole point of a store: a
 *      graph is baked once and fed many times.
 *
 *   3. PREPARE. A walk records what it visits into the root's own path record,
 *      and a record with no room takes room as it goes - an allocation inside
 *      the walk. So the room is made here instead, sized for what a walk from
 *      this root can need: one entry per level, and no walk goes deeper than
 *      the graph (see c_dcg_root_node_eval_path_reserve). A baked graph's walk
 *      allocates nothing.
 *
 * A BAKE IS ASKED FOR, AND IT IS ALL OR NOTHING: a graph that fails either half
 * of the verification is NOT locked and NOT prepared, so a caller that ignores
 * the report is left with a graph that does not work rather than with one that
 * half works. A bake that succeeds is IDEMPOTENT - asked again it locks
 * nothing, seals nothing and prepares nothing, and says so in the report.
 *
 * THE REPORT is what the pass found and what it affected, and it is the
 * caller's to hand in; a caller that wants only the code passes NULL. The
 * verification's findings ride in the same three fields as the operands' -
 * `code`, `node` and `errors` name the FIRST problem, in the order the pass asks
 * its questions - and what the pass CHANGED is counted beside them: how many
 * nodes it locked, how many stores it sealed, and how much room it made.
 *
 * The header is C only: it is above every family (it includes them all, and the
 * protocol they are evaluated by) and nothing includes it. A Cython module that
 * binds it does so with a textual `cdef extern from`, so it creates no pxd edge
 * and no cycle - see DEPENDENCY.md section 3.2.
 */

// ========== Constants ==========

/**
 * @brief What a bake is asked for.
 *
 * A bake that is asked for nothing in particular does the whole of it, which is
 * why the default is NONE rather than a flag with the work in it: the flags name
 * the departures from a bake, and there is one - asking for the verdict without
 * the lockdown.
 */
typedef enum dcg_bake_flag {
    DCG_BAKE_FLAG_NONE          = 0,      // Verify, lock and prepare - the whole of a bake.
    DCG_BAKE_FLAG_VALIDATE_ONLY = 1 << 0  // Verify and report; lock nothing, prepare nothing.
} dcg_bake_flag;

// ========== Structs ==========

/**
 * @brief What a bake is asked WITH.
 *
 * The flags are the whole of it today, and the struct is here rather than a
 * plain argument for the reason a C API has one: what a pass is asked grows,
 * and growing a struct leaves every call site that already exists alone.
 *
 * Zero-initialize it, or call c_dcg_bake_input_init(): a bake asked with no
 * flags set is the whole of a bake.
 */
typedef struct dcg_bake_input {
    uint64_t flags;  // dcg_bake_flag bits.
} dcg_bake_input;

/**
 * @brief What a bake found, and what it affected.
 *
 * The first five fields are the shape `dcg_validate_report` has, because the
 * pass runs that check: `code` is the first problem (DCG_OK when the graph is
 * baked), `node` the node it was found on, `errors` how many were found, and
 * `nodes` and `depth` what the walk that ran covered.
 *
 * The last three are the other half of the pass - what it CHANGED, which a
 * re-bake reports as nothing:
 *
 *   - `locked`: nodes this pass froze. A graph in the graph's own size means
 *     nothing was locked and the graph was already baked;
 *   - `sealed`: stores this pass froze. Each one is counted once, however many
 *     reads name it;
 *   - `capacity`: entries the root's record was prepared with. 0 when the pass
 *     did not prepare one - a validate-only bake, or one that failed.
 */
typedef struct dcg_bake_report {
    dcg_ret_code code;      // DCG_OK when the graph is baked, or the first problem that stopped the pass.
    dcg_node*    node;      // The node that produced it; NULL when it was not a node's.
    size_t       errors;    // Problems found: the graph's shape, then its operators' operands.
    size_t       nodes;     // Nodes the pass walked - the branches, and the operands they read.
    size_t       depth;     // The deepest level the walk reached.
    size_t       locked;    // Nodes this pass locked.
    size_t       sealed;    // Stores this pass sealed.
    size_t       capacity;  // Entries the root's record was prepared with.
} dcg_bake_report;

// ========== Forward Declarations ==========

// The input, and the report a caller wants of its own
static inline void             c_dcg_bake_input_init(dcg_bake_input* input);
static inline dcg_bake_report* c_dcg_bake_report_new(allocator_protocol* allocator);
static inline void             c_dcg_bake_report_free(dcg_bake_report* report);

// The node's own halves
static inline void             c_dcg_bake_fail(dcg_bake_report* report, const dcg_node* node, dcg_ret_code err, bool* valid);
static inline void             c_dcg_bake_node_check(const dcg_node* node, dcg_bake_report* report, bool* valid);
static inline void             c_dcg_bake_node_lock(dcg_node* node, dcg_bake_report* report);

// The walk
static inline void             c_dcg_bake_walk(dcg_node* node, dcg_bake_report* report, size_t depth, bool* valid);
static inline void             c_dcg_bake_lock_walk(dcg_node* node, dcg_bake_report* report);
static inline void             c_dcg_bake_unmark_walk(dcg_node* node);
static inline int              c_dcg_bake_prepare_record(dcg_root_node* root, dcg_bake_report* report);
static inline int              c_dcg_root_node_bake(dcg_root_node* root, const dcg_bake_input* input, dcg_bake_report* report);

// ========== The Input ==========

/**
 * @brief What a bake is asked with when the caller does not say.
 *
 * A bake asked for nothing is the whole of a bake: verify, lock, prepare. The
 * function is here so a caller does not have to know what "nothing" is spelled
 * as, which is what keeps a field added later from becoming an uninitialized
 * one at every call site.
 *
 * @param input  Input to fill (NULL-safe: nothing to fill).
 */
static inline void             c_dcg_bake_input_init(dcg_bake_input* input) {
    if (!input) return;
    memset(input, 0, sizeof(*input));
}

// ========== The Report's Own Lifecycle ==========

/**
 * @brief A report of its own, so a caller can ask for one and keep it.
 *
 * The door writes a report the caller hands in, which is what the layer's own
 * callers want: a bake is asked once and read where it happened. A report of its
 * own is the other half of the same API - what a binding layer hands back to a
 * caller who asked for the answer rather than for the graph to be baked - so the
 * type carries the two functions every owning type has, and they are the two it
 * needs: a report owns nothing behind its block (the node it may name belongs to
 * the graph), so there is no payload for a `_dealloc` to release and it goes with
 * `_free` alone.
 *
 * A fresh report says what a bake that never ran says: DCG_OK, no node, no
 * counts. It is a report to be FILLED, and the door fills it in place.
 *
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The report, or NULL on OOM.
 */
static inline dcg_bake_report* c_dcg_bake_report_new(allocator_protocol* allocator) {
    dcg_bake_report* report = (dcg_bake_report*) c_ap_alloc(sizeof(dcg_bake_report), allocator);
    if (!report) return NULL;

    memset(report, 0, sizeof(*report));
    return report;
}

/**
 * @brief Release a report of its own.
 *
 * Only for a report from c_dcg_bake_report_new(). The report a caller hands the
 * door is that caller's - a local, a field of a larger struct - and nothing here
 * frees it.
 *
 * @param report  Report to free (NULL-safe).
 */
static inline void c_dcg_bake_report_free(dcg_bake_report* report) {
    if (!report) return;
    c_ap_free_owned(report);
}

// ========== The Node's Own Halves ==========

/**
 * @brief Record a problem the bake found, and say the graph is not bakeable.
 *
 * The sibling of c_dcg_validate_fail, over the pass's own report: the first
 * problem is the one a caller is told about - it is the one the pass stopped
 * asking questions after - and the rest are counted.
 *
 * @param report  Report being filled (NULL-safe: counting is then skipped).
 * @param node    Node the problem was found on.
 * @param err     Error code.
 * @param valid   Bakeability accumulator (NULL-safe).
 */
static inline void c_dcg_bake_fail(dcg_bake_report* report, const dcg_node* node, dcg_ret_code err, bool* valid) {
    if (report) {
        report->errors++;
        if (report->code == DCG_OK) {
            report->code = err;
            report->node = (dcg_node*) node;
        }
    }
    if (valid) *valid = false;
}

/**
 * @brief What an evaluation of ONE node assumes, and what the bake owes it.
 *
 * Only an operator node has anything to check here: every other family settles
 * its value at birth (a literal is its value, an action is the node itself, a
 * root is true) or carries its own rule from the store that built it, and none
 * of them reads a field this pass could find missing.
 *
 * An operator node is the one that does: its rule runs each operand and applies
 * the operator, and the three things it takes for granted are exactly the three
 * checked below. Each is refused with the code the layer would otherwise report
 * one evaluation at a time - DCG_ERR_TYPE for a node that has no evaluation and
 * for an operator its arity does not apply, and DCG_ERR_UNBOUND for an operand
 * slot with nothing bound to it, because the value that rule would have read is
 * the one that never landed.
 *
 * @param node    Node to check.
 * @param report  Report to record a problem in.
 * @param valid   Bakeability accumulator.
 */
static inline void c_dcg_bake_node_check(const dcg_node* node, dcg_bake_report* report, bool* valid) {
    if (!c_dcg_node_type_is_op(node->ntype)) return;

    const dcg_expression_node* expr  = (const dcg_expression_node*) node;
    size_t                     arity = c_dcg_node_type_arity(node->ntype);

    /* An arity of none: the family's head type, and a call. Neither has a rule
     * to run - a call's callee is composed into its display text and stored
     * nowhere - so a graph holding one has nothing to evaluate it with. */
    if (arity == 0) {
        c_dcg_bake_fail(report, node, DCG_ERR_TYPE, valid);
        return;
    }

    /* The operator the node applies, and the arity applying it, are ONE fact
     * settled at build time: an operator of another arity installs nothing and
     * leaves the refusing rule standing (see c_dcg_node_expr_set_op), so a node
     * whose pair does not agree is a node that can never produce a value. An
     * if-expression carries no operator its arity applies - its rule IS the
     * arity's - so a code on it decides nothing and is not asked about. */
    if (node->ntype != DCG_NODE_TERNARY && c_dcg_op_code_node_type(expr->op) != node->ntype) {
        c_dcg_bake_fail(report, node, DCG_ERR_TYPE, valid);
        return;
    }

    /* The array its rules index by arity: shorter than the arity is a read past
     * the block, and the block is the node's own - so this is checked before a
     * single slot is, and the slots are only read once it holds. */
    if (expr->n_args != arity) {
        c_dcg_bake_fail(report, node, DCG_ERR_TYPE, valid);
        return;
    }

    for (size_t i = 0; i < expr->n_args; i++) {
        if (!expr->components[i]) c_dcg_bake_fail(report, node, DCG_ERR_UNBOUND, valid);
    }
}

/**
 * @brief Lock one node and seal the store behind it, if it reads one.
 *
 * The two locks a bake applies, in the one place they are applied:
 *
 *   - the NODE is frozen, whatever type it is. A node the graph was built from
 *     is not a place to hang more from, and the node layer already refuses the
 *     operations a frozen parent forbids (see dcg_node_flag);
 *   - the STORE is sealed, when the node is a read naming one. The block a
 *     resolved read points into stops moving the moment the store can no longer
 *     grow, and a store can no longer grow the moment it is frozen (see
 *     c_dcg_mapping_lgroup_get_create_slot - the one place an entry is born).
 *
 * Both are counted as they are taken and only when they are taken, which is what
 * makes a re-bake report nothing: a node that was frozen and a store that was
 * sealed are left alone the second time.
 *
 * A read that names no store - one bound by hand to another node's slot, the
 * case c_dcg_node_eval_default answers - has no store to seal; a read naming a
 * group that is not a store has none either.
 *
 * @param node    Node to lock.
 * @param report  Report the counts go to.
 */
static inline void c_dcg_bake_node_lock(dcg_node* node, dcg_bake_report* report) {
    if (!(node->flags & DCG_NODE_FLAG_FROZEN)) {
        node->flags |= DCG_NODE_FLAG_FROZEN;
        report->locked++;
    }
    if (node->ntype != DCG_NODE_VARIABLE) return;

    dcg_logic_group* group = ((dcg_variable_node*) node)->logic_group;
    if (!group || group->lgtype != DCG_LG_MAPPING) return;

    dcg_mapping_lgroup* mapping = (dcg_mapping_lgroup*) group;
    if (mapping->frozen) return;

    mapping->frozen = true;
    report->sealed++;
}

// ========== The Walk ==========

/**
 * @brief Walk the graph a bake is asked about, checking what it reaches.
 *
 * The edges are TWO, and both are the graph: a node's CHILDREN, which are the
 * branches its value selects between, and - for an operator node - its
 * COMPONENTS, which are the operands its rule runs. An operand is not a child
 * of anything: it is what the node was built from, and the evaluation reaches it
 * through the node rather than through the tree. A walk that followed only
 * children would therefore walk past every read, every literal and every
 * operator composed into one - which is most of the graph.
 *
 * Each node is marked as it is reached and skipped when it is reached again, so
 * one node is checked once however many operands name it - and so a graph whose
 * operands lead back to where they came from is walked to its end rather than
 * forever. The marks are the check walk's, and the pass that follows it - the
 * lockdown, or the unmarking a validate-only bake owes - is what takes them off
 * again.
 *
 * What that terminating walk does NOT do is refuse an operand edge that points
 * back into the graph: what this pass owes a graph is that every node an
 * evaluation will run has what its rule needs, and that it reaches each of them
 * once - an operand graph's SHAPE beyond that is a question about the graph, not
 * about a node, and a build that composes its operands from other operands
 * (which is every build the wrappers allow) cannot make one.
 *
 * @param node    Node to walk.
 * @param report  Report being filled.
 * @param depth   How deep the node sits.
 * @param valid   Bakeability accumulator.
 */
static inline void c_dcg_bake_walk(dcg_node* node, dcg_bake_report* report, size_t depth, bool* valid) {
    if (!node || (node->flags & DCG_NODE_FLAG_VISITED)) return;

    node->flags |= DCG_NODE_FLAG_VISITED;
    report->nodes++;
    if (depth > report->depth) report->depth = depth;

    c_dcg_bake_node_check(node, report, valid);

    for (dcg_node* child = node->children; child; child = child->next_sibling) c_dcg_bake_walk(child, report, depth + 1, valid);

    if (!c_dcg_node_type_is_op(node->ntype)) return;

    dcg_expression_node* expr = (dcg_expression_node*) node;
    for (size_t i = 0; i < expr->n_args; i++) c_dcg_bake_walk(expr->components[i], report, depth + 1, valid);
}

/**
 * @brief Apply the lockdown to every node the check walk reached.
 *
 * The marks are CONSUMED here rather than consulted: a node's mark is taken off
 * as the walk enters it, which is what makes this walk reach each node exactly
 * once and leave the graph's scratch bit as it found it. It runs only over a
 * graph the check walk passed, so what it walks is what was verified.
 *
 * @param node    Node to lock.
 * @param report  Report the counts go to.
 */
static inline void c_dcg_bake_lock_walk(dcg_node* node, dcg_bake_report* report) {
    if (!node || !(node->flags & DCG_NODE_FLAG_VISITED)) return;

    node->flags &= ~(uint32_t) DCG_NODE_FLAG_VISITED;
    c_dcg_bake_node_lock(node, report);

    for (dcg_node* child = node->children; child; child = child->next_sibling) c_dcg_bake_lock_walk(child, report);

    if (!c_dcg_node_type_is_op(node->ntype)) return;

    dcg_expression_node* expr = (dcg_expression_node*) node;
    for (size_t i = 0; i < expr->n_args; i++) c_dcg_bake_lock_walk(expr->components[i], report);
}

/**
 * @brief Take the check walk's marks off, changing nothing else.
 *
 * What a pass that decided NOT to lock the graph owes it: the bit is scratch,
 * and the walks that use it for their own bookkeeping later - the teardown of a
 * whole graph above all - take a marked node for one they have already been to.
 *
 * @param node  Node to unmark.
 */
static inline void c_dcg_bake_unmark_walk(dcg_node* node) {
    if (!node || !(node->flags & DCG_NODE_FLAG_VISITED)) return;

    node->flags &= ~(uint32_t) DCG_NODE_FLAG_VISITED;

    for (dcg_node* child = node->children; child; child = child->next_sibling) c_dcg_bake_unmark_walk(child);

    if (!c_dcg_node_type_is_op(node->ntype)) return;

    dcg_expression_node* expr = (dcg_expression_node*) node;
    for (size_t i = 0; i < expr->n_args; i++) c_dcg_bake_unmark_walk(expr->components[i]);
}

/**
 * @brief Make the room the root's record needs, before a walk needs it.
 *
 * The walk the bake is preparing for records one node per level into the root's
 * own record, and a record that runs out of room grows inside the walk. That is
 * the only allocation an evaluation makes, and it is the one this removes: the
 * room is made here, sized for what a walk from this root can ever use - the
 * height of the graph plus the node it starts from - so the walk that follows a
 * bake never grows anything.
 *
 * A record that already has room is left as it is, and reported with the room
 * it has: a graph whose root was walked before it was baked is prepared already.
 *
 * @param root    Root whose record to make room for.
 * @param report  Report receiving the room.
 * @return DCG_OK, or DCG_ERR_OOM when the room could not be made.
 */
static inline int c_dcg_bake_prepare_record(dcg_root_node* root, dcg_bake_report* report) {
    dcg_node_eval_path* path = &root->eval_path;

    if (!path->node || !path->eval_val) {
        int ret_code = c_dcg_root_node_eval_path_reserve(root, 0); /* 0: the graph's own height, plus 1 */
        if (ret_code != DCG_OK) return ret_code;
    }

    report->capacity = path->capacity;
    return DCG_OK;
}

// ========== The Door ==========

/**
 * @brief Bake a graph from its root: verify it, lock it, and prepare it.
 *
 * The whole of the protocol, in the order its three parts have to happen in:
 *
 *   1. the graph's structure is checked first (c_dcg_node_validate). A graph
 *      that fails it is not walked any further: what the operand walk would be
 *      reading is a shape nobody has established, and the caller has something
 *      to fix before a second opinion is worth anything;
 *   2. every node the walk reaches is checked for what an evaluation of it
 *      assumes (c_dcg_bake_node_check);
 *   3. only then is the graph locked and sealed (c_dcg_bake_lock_walk) and its
 *      record prepared (c_dcg_bake_prepare_record) - so a graph that fails is
 *      left exactly as it was found, and a caller that ignored the report has a
 *      graph that does not work rather than one that half works. A
 *      validate-only bake stops after the second part, having taken its marks
 *      off the graph.
 *
 * @param root    Root of the graph to bake. A root is the only entry, so this is
 *                the only door.
 * @param input   What the bake is asked with; NULL asks for the whole of one.
 * @param report  Receives what was found and what was affected (may be NULL: the
 *                return code then carries the answer on its own).
 * @return DCG_OK when the graph is baked, or the DCG_ERR_* that stopped it.
 */
static inline int c_dcg_root_node_bake(dcg_root_node* root, const dcg_bake_input* input, dcg_bake_report* report) {
    if (!root) return DCG_ERR_INVALID_ARG;

    /* A caller that wants only the verdict gets one: the report is written to
     * the caller's slot when there is one, and to a local when there is not, so
     * everything below reads one report either way. */
    dcg_bake_report  local;
    dcg_bake_report* out   = report ? report : &local;
    uint64_t         flags = input ? input->flags : (uint64_t) DCG_BAKE_FLAG_NONE;

    memset(out, 0, sizeof(*out));

    /* The graph's own shape first: it is what every walk below assumes, and the
     * node layer's report is the one a caller reads for it. */
    dcg_validate_report structure;
    if (!c_dcg_node_validate(&root->base, &structure)) {
        out->code   = structure.code;
        out->node   = structure.node;
        out->errors = structure.errors;
        out->nodes  = structure.nodes;
        out->depth  = structure.depth;
        return out->code;
    }

    bool valid = true;
    c_dcg_bake_walk(&root->base, out, 0, &valid);

    if (!valid || (flags & DCG_BAKE_FLAG_VALIDATE_ONLY)) {
        c_dcg_bake_unmark_walk(&root->base);
        return out->code;
    }

    c_dcg_bake_lock_walk(&root->base, out);

    int ret_code = c_dcg_bake_prepare_record(root, out);
    if (ret_code != DCG_OK) {
        /* The graph is locked - it was verified, and the lockdown is what the
         * verification bought - but it has no room to be walked in, so the pass
         * says what stopped it rather than reporting a bake that is not ready. */
        out->code = ret_code;
        out->node = NULL;
    }
    return ret_code;
}

#endif  // C_DCG_BAKE_BAKE_H
