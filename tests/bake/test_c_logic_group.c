/*
 * c_logic_group.h - the scopes a graph is built inside, and the manager whose
 * stacks a build runs on.
 *
 * These are the BUILD-time halves of the capi's LogicGroup and LGM: opening
 * and closing a group, what a node gets from the groups around it, and what a
 * break does while a graph is still being assembled. What the capi calls the
 * runtime half - breaking while EVALUATING - is a control-flow signal, and it
 * lands with the evaluator; nothing here evaluates anything.
 *
 * The manager is a local in every test, which is the case it has to work in: it
 * is the caller's struct, not an allocator block, and it keeps the allocator it
 * was initialized with because of it.
 */

#include <decision_graph/decision_tree/bake/c_logic_group.h>

#include <decision_graph/decision_tree/bake/c_action.h>
#include <decision_graph/decision_tree/bake/c_collections.h>
#include <decision_graph/decision_tree/bake/c_const.h>
#include <decision_graph/decision_tree/bake/c_hierarchy.h>

#include "test_util.h"

static void test_group_lifecycle(void) {
    /* A base group is metadata and nothing else: a kind, a name, and a parent
     * the manager fills in when it is entered. Nothing to carry means nothing
     * to release beyond the header. */
    dcg_logic_group* group = c_dcg_logic_group_new(DCG_LG_BASE, "check_open", NULL);
    DCG_CHECK(group != NULL);
    DCG_CHECK_INT(group->lgtype, DCG_LG_BASE);
    DCG_CHECK_STR(group->name, "check_open");
    DCG_CHECK(group->parent == NULL);
    dcg_t_trace_group("base group", group);

    /* The header is the whole of a base group; a mapping is what adds a store
     * beside it. That is "carries no values" made concrete. */
    DCG_CHECK(sizeof(dcg_mapping_lgroup) > sizeof(dcg_logic_group));
    c_dcg_logic_group_free(group);

    /* A group's name is its own copy, nested under it. */
    char name[] = "check_open";
    group       = c_dcg_logic_group_new(DCG_LG_BASE, name, NULL);
    name[0]     = 'X';
    DCG_CHECK_STR(group->name, "check_open");
    c_dcg_logic_group_free(group);

    /* A name is optional: an unnamed group is legal, it just cannot be found by
     * name in the registry. */
    group = c_dcg_logic_group_new(DCG_LG_BASE, NULL, NULL);
    DCG_CHECK(group != NULL);
    DCG_CHECK(group->name == NULL);
    c_dcg_logic_group_free(group);

    c_dcg_logic_group_free(NULL);

    DCG_CHECK_STR(c_dcg_logic_group_type_name(DCG_LG_BASE), "BASE");
    DCG_CHECK_STR(c_dcg_logic_group_type_name(DCG_LG_MAPPING), "MAPPING");
    DCG_CHECK_STR(c_dcg_logic_group_type_name((dcg_logic_group_type) 0x7fff), "UNKNOWN");
}

static void test_manager_lifecycle(void) {
    dcg_logic_group_manager mgr;
    dcg_logic_group*        group = c_dcg_logic_group_new(DCG_LG_BASE, "g", NULL);

    DCG_CHECK_INT(c_dcg_lgm_init(&mgr, NULL), DCG_OK);
    DCG_CHECK(mgr.allocator == NULL); /* the plain heap, which is what the caller asked for */
    DCG_CHECK(c_dcg_lgm_active_group(&mgr) == NULL);
    DCG_CHECK(c_dcg_lgm_active_node(&mgr) == NULL);
    DCG_CHECK_INT(c_dcg_lgm_breakpoint_count(&mgr), 0);
    dcg_t_trace_lgm("fresh", &mgr);

    /* clear() drops the registry and the frames and leaves the manager usable -
     * the capi's clear(), which is what a build calls between graphs. */
    DCG_CHECK_INT(c_dcg_lgm_enter_group(&mgr, group), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_register(&mgr, group), DCG_OK);
    c_dcg_lgm_clear(&mgr);
    DCG_CHECK(c_dcg_lgm_active_group(&mgr) == NULL);
    DCG_CHECK(c_dcg_lgm_find(&mgr, "g", 1) == NULL);
    DCG_CHECK_INT(c_dcg_lgm_enter_group(&mgr, group), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_exit_group(&mgr, group), DCG_OK);

    /* A group never entered may be left: NULL leaves the innermost, and with
     * nothing open there is nothing to leave. */
    DCG_CHECK_INT(c_dcg_lgm_exit_group(&mgr, NULL), DCG_ERR_NOT_FOUND);

    c_dcg_lgm_dealloc(&mgr); /* a stack manager: the clean half, not the block's */
    c_dcg_lgm_free(NULL);
    c_dcg_lgm_clear(NULL);
    DCG_CHECK_INT(c_dcg_lgm_init(NULL, NULL), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_lgm_enter_group(NULL, group), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_lgm_enter_group(&mgr, NULL), DCG_ERR_INVALID_ARG);

    /* The other half of the pair: a manager from c_dcg_lgm_new is an allocator
     * block, and freeing it releases the block as well as the contents. */
    dcg_logic_group_manager* heap = c_dcg_lgm_new(NULL);
    DCG_CHECK(heap != NULL);
    DCG_CHECK_INT(c_dcg_lgm_enter_group(heap, group), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_register(heap, group), DCG_OK);
    DCG_CHECK(c_dcg_lgm_active_group(heap) == group);
    c_dcg_lgm_free(heap);
    c_dcg_lgm_free(NULL);
    heap = c_dcg_lgm_new(NULL);           /* a fresh one carries nothing over */
    DCG_CHECK(heap != NULL);
    DCG_CHECK(c_dcg_lgm_active_group(heap) == NULL);
    DCG_CHECK(c_dcg_lgm_find(heap, "g", 1) == NULL); /* the registry starts empty */
    c_dcg_lgm_free(heap);

    c_dcg_logic_group_free(group);
}

static void test_registry(void) {
    dcg_logic_group_manager mgr;
    dcg_logic_group*        open     = c_dcg_logic_group_new(DCG_LG_BASE, "check_open", NULL);
    dcg_logic_group*        close    = c_dcg_logic_group_new(DCG_LG_BASE, "check_close", NULL);
    dcg_logic_group*        twin     = c_dcg_logic_group_new(DCG_LG_BASE, "check_open", NULL);
    dcg_logic_group*        nameless = c_dcg_logic_group_new(DCG_LG_BASE, NULL, NULL);

    (void) dcg_t_lgm(&mgr);

    DCG_CHECK_INT(c_dcg_lgm_register(&mgr, open), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_register(&mgr, close), DCG_OK);

    DCG_CHECK(c_dcg_lgm_find(&mgr, "check_open", 10) == open);
    DCG_CHECK(c_dcg_lgm_find(&mgr, "check_close", 11) == close);
    DCG_CHECK(c_dcg_lgm_find(&mgr, "missing", 7) == NULL);
    DCG_CHECK(c_dcg_lgm_find(&mgr, "check_open", 5) == NULL); /* a prefix is not the name */
    DCG_CHECK(c_dcg_lgm_find(&mgr, "check_open_", 11) == NULL);
    DCG_CHECK(c_dcg_lgm_find(NULL, "check_open", 10) == NULL);
    DCG_CHECK(c_dcg_lgm_find(&mgr, NULL, 0) == NULL);

    /* The name is the identity: a second group under it is refused, and the
     * first one stays the one the name finds. */
    DCG_CHECK_INT(c_dcg_lgm_register(&mgr, twin), DCG_ERR_DUPLICATE);
    DCG_CHECK(c_dcg_lgm_find(&mgr, "check_open", 10) == open);

    /* A group with no name cannot be looked up, so there is nothing to register. */
    DCG_CHECK_INT(c_dcg_lgm_register(&mgr, nameless), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_lgm_register(&mgr, NULL), DCG_ERR_INVALID_ARG);

    /* The registry borrows: freeing the manager leaves its groups alone. */
    c_dcg_lgm_dealloc(&mgr); /* a stack manager: the clean half, not the block's */
    DCG_CHECK_STR(open->name, "check_open");

    c_dcg_logic_group_free(open);
    c_dcg_logic_group_free(close);
    c_dcg_logic_group_free(twin);
    c_dcg_logic_group_free(nameless);
}

static void test_group_stack(void) {
    dcg_logic_group_manager mgr;
    dcg_logic_group*        outer = c_dcg_logic_group_new(DCG_LG_BASE, "outer", NULL);
    dcg_logic_group*        inner = c_dcg_logic_group_new(DCG_LG_BASE, "inner", NULL);

    (void) dcg_t_lgm(&mgr);

    DCG_CHECK_INT(c_dcg_lgm_enter_group(&mgr, outer), DCG_OK);
    DCG_CHECK(c_dcg_lgm_active_group(&mgr) == outer);
    DCG_CHECK(outer->parent == NULL); /* entered first: it nests in nothing */

    /* A group entered from within another takes it as its parent - the capi's
     * rule, and the reason a nested scope can be named by walking up. */
    DCG_CHECK_INT(c_dcg_lgm_enter_group(&mgr, inner), DCG_OK);
    DCG_CHECK(c_dcg_lgm_active_group(&mgr) == inner);
    DCG_CHECK(inner->parent == outer);
    DCG_CHECK_INT(mgr.n_groups, 2);
    dcg_t_trace_lgm("nested groups", &mgr);

    /* Leaving out of order is refused, and nothing is popped. */
    DCG_CHECK_INT(c_dcg_lgm_exit_group(&mgr, outer), DCG_ERR_BUSY);
    DCG_CHECK_INT(mgr.n_groups, 2);

    DCG_CHECK_INT(c_dcg_lgm_exit_group(&mgr, inner), DCG_OK);
    DCG_CHECK(c_dcg_lgm_active_group(&mgr) == outer);

    /* NULL leaves the innermost one. */
    DCG_CHECK_INT(c_dcg_lgm_exit_group(&mgr, NULL), DCG_OK);
    DCG_CHECK(c_dcg_lgm_active_group(&mgr) == NULL);

    c_dcg_lgm_dealloc(&mgr); /* a stack manager: the clean half, not the block's */
    c_dcg_logic_group_free(outer);
    c_dcg_logic_group_free(inner);
}

static void test_node_stack(void) {
    dcg_logic_group_manager mgr;
    dcg_node*               root  = dcg_t_node_root("Entry Point");
    dcg_node*               first = dcg_t_node_plain("first");

    (void) dcg_t_lgm(&mgr);

    DCG_CHECK(c_dcg_lgm_active_node(&mgr) == NULL);
    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, root), DCG_ERR_NOT_FOUND);
    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, NULL), DCG_ERR_INVALID_ARG);

    /* The first node has no active node to attach to, so it is simply taken as
     * active: the graph it heads is its own. */
    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, root), DCG_OK);
    DCG_CHECK(c_dcg_lgm_active_node(&mgr) == root);
    DCG_CHECK(root->parent == NULL);
    DCG_CHECK_INT(c_dcg_node_child_count(root), 1); /* the single arm its own enter reserved */
    DCG_CHECK_INT(root->children->ntype, DCG_NODE_PLACEHOLDER);

    /* The next one fills the active node's placeholder slot: that is what links
     * a branch into the graph as it is built. */
    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, first), DCG_OK);
    DCG_CHECK(c_dcg_lgm_active_node(&mgr) == first);
    DCG_CHECK_INT(c_dcg_node_child_count(root), 1);
    DCG_CHECK(c_dcg_node_first_child(root) == first);
    DCG_CHECK(first->condition_to_parent == DCG_NO_CONDITION); /* a root is unconditioned */
    DCG_CHECK(root->children == first);                        /* no placeholder was left behind */
    DCG_CHECK_INT(mgr.n_nodes, 2);
    dcg_t_trace_lgm("entered root, first", &mgr);

    /* Leaving out of order is refused; leaving the innermost is the pop. */
    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, root), DCG_ERR_BUSY);
    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, first), DCG_OK);
    DCG_CHECK(c_dcg_lgm_active_node(&mgr) == root);
    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, root), DCG_OK);
    DCG_CHECK(c_dcg_lgm_active_node(&mgr) == NULL);

    /* An action is a leaf: there is nothing to build inside one. */
    dcg_node* action = dcg_t_node_action(DCG_NODE_LONGACTION, "long");
    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, action), DCG_ERR_TYPE);
    DCG_CHECK_INT(mgr.n_nodes, 0);

    c_dcg_lgm_dealloc(&mgr); /* a stack manager: the clean half, not the block's */
    /* root, first, and the two arms first's own enter reserved and left unfilled */
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4);
    c_dcg_node_free(action);
}

static void test_labels(void) {
    dcg_logic_group_manager mgr;
    dcg_logic_group*        outer = c_dcg_logic_group_new(DCG_LG_BASE, "outer", NULL);
    dcg_logic_group*        inner = c_dcg_logic_group_new(DCG_LG_BASE, "inner", NULL);
    dcg_node*               node  = dcg_t_node_plain("n");

    (void) dcg_t_lgm(&mgr);

    /* No group open: nothing to label with. */
    DCG_CHECK_INT(c_dcg_lgm_label_node(&mgr, node), 0);
    DCG_CHECK_INT(c_dcg_node_label_count(node), 0);

    DCG_CHECK_INT(c_dcg_lgm_enter_group(&mgr, outer), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_label_node(&mgr, node), 1);
    DCG_CHECK(c_dcg_node_has_label(node, "outer"));
    DCG_CHECK(!c_dcg_node_has_label(node, "inner"));

    /* A node belongs to every group open around it, not just the innermost. */
    DCG_CHECK_INT(c_dcg_lgm_enter_group(&mgr, inner), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_label_node(&mgr, node), 1);
    DCG_CHECK(c_dcg_node_has_label(node, "outer"));
    DCG_CHECK(c_dcg_node_has_label(node, "inner"));
    DCG_CHECK_INT(c_dcg_node_label_count(node), 2);

    /* Labeling twice adds nothing: a name already on the node is skipped, which
     * is what lets a builder call this once per node without watching the stack. */
    DCG_CHECK_INT(c_dcg_lgm_label_node(&mgr, node), 0);
    DCG_CHECK_INT(c_dcg_node_label_count(node), 2);

    /* The labels outlive the groups: they are the node's own copies. */
    DCG_CHECK_INT(c_dcg_lgm_exit_group(&mgr, inner), DCG_OK);
    DCG_CHECK(c_dcg_node_has_label(node, "inner"));

    DCG_CHECK_INT(c_dcg_lgm_label_node(NULL, node), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_lgm_label_node(&mgr, NULL), DCG_ERR_INVALID_ARG);

    c_dcg_lgm_dealloc(&mgr); /* a stack manager: the clean half, not the block's */
    c_dcg_node_free(node);
    c_dcg_logic_group_free(outer);
    c_dcg_logic_group_free(inner);
}

static void test_break_inspection(void) {
    dcg_logic_group_manager mgr;
    dcg_logic_group*        group = c_dcg_logic_group_new(DCG_LG_BASE, "check_open", NULL);
    dcg_node*               root  = dcg_t_node_root("Entry Point");
    dcg_node*               guard = dcg_t_node_plain("guard");

    (void) dcg_t_lgm(&mgr);

    /* With no active node there is nothing to break out of, and that is not an
     * error: a break at the top of a graph is an empty branch, not a mistake. */
    DCG_CHECK_INT(c_dcg_lgm_break_inspection(&mgr, group), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_breakpoint_count(&mgr), 0);

    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, root), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, guard), DCG_OK);

    /* The break swaps the active node's placeholder slot for a breakpoint that
     * names the group, and queues it. */
    DCG_CHECK_INT(c_dcg_lgm_break_inspection(&mgr, group), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_breakpoint_count(&mgr), 1);
    /* The guard reserved both of its arms on the way in; the break took the arm
     * the fill would take - the TRUE one, which is the first of the pair. */
    DCG_CHECK_INT(c_dcg_node_child_count(guard), 2);

    dcg_node*            raised     = c_dcg_node_child_by_condition(guard, DCG_TRUE_CONDITION);
    dcg_breakpoint_node* breakpoint = (dcg_breakpoint_node*) raised;
    DCG_CHECK_INT(raised->ntype, DCG_NODE_BREAKPOINT);
    DCG_CHECK(breakpoint->break_from == group);                   /* borrowed: the group outlives the graph */
    DCG_CHECK(!breakpoint->await_connection);                     /* the group is still open */
    DCG_CHECK(raised->eval_ctx.flags & DCG_EVAL_FLAG_BREAKPOINT); /* an evaluator stops here */
    DCG_CHECK(raised->autogen);
    DCG_CHECK(c_dcg_node_last_child(guard)->ntype == DCG_NODE_PLACEHOLDER); /* the FALSE arm still holds its slot */
    DCG_CHECK(raised->next_sibling == c_dcg_node_last_child(guard));        /* and the placeholder it displaced is gone */

    dcg_t_trace_tree("tree(root -> guard -> breakpoint)", root);

    DCG_CHECK_INT(c_dcg_lgm_break_inspection(NULL, group), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_lgm_break_inspection(&mgr, NULL), DCG_ERR_INVALID_ARG);

    c_dcg_lgm_dealloc(&mgr); /* a stack manager: the clean half, not the block's */
    /* root, guard, the FALSE arm guard still holds, and the breakpoint */
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4);
    c_dcg_logic_group_free(group);
}

static void test_break_resume(void) {
    /* The whole shape of a break, end to end, in the order a build runs it. */
    dcg_logic_group_manager mgr;
    dcg_logic_group*        group = c_dcg_logic_group_new(DCG_LG_BASE, "check_open", NULL);
    dcg_node*               root  = dcg_t_node_root("Entry Point");
    dcg_node*               guard = dcg_t_node_plain("guard");
    dcg_node*               after = dcg_t_node_plain("after");

    (void) dcg_t_lgm(&mgr);

    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, root), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, guard), DCG_OK);

    DCG_CHECK_INT(c_dcg_lgm_enter_group(&mgr, group), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_break_inspection(&mgr, group), DCG_OK);

    /* The break took the arm the fill would take - the TRUE one. */
    dcg_breakpoint_node* breakpoint = (dcg_breakpoint_node*) c_dcg_node_child_by_condition(guard, DCG_TRUE_CONDITION);
    DCG_CHECK(breakpoint != NULL);

    /* Leaving the group is what makes the break real: the breakpoint starts
     * waiting for the node it resumes into. */
    DCG_CHECK(!breakpoint->await_connection);
    DCG_CHECK_INT(c_dcg_lgm_exit_group(&mgr, group), DCG_OK);
    DCG_CHECK(breakpoint->await_connection);

    /* The next node entered is the one it resumes into, and the breakpoint is
     * retired from the queue. */
    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, after), DCG_OK);
    DCG_CHECK(!breakpoint->await_connection);
    DCG_CHECK_INT(c_dcg_lgm_breakpoint_count(&mgr), 0);
    /* The breakpoint points at the node it resumes into. How many entries its
     * child list appears to hold is NOT the contract: a joined node stands in
     * two parents' lists at once, and its sibling chain belongs to whichever of
     * them linked it last - here the guard, which is why the breakpoint's list
     * reads on into the guard's. */
    DCG_CHECK(c_dcg_node_first_child(&breakpoint->base) == after);

    /* The resumed node is a JOIN: the breakpoint names it, and it also takes
     * the slot the build continued with, so its own parent is the guard. That
     * is the capi's shape - the block is reached twice and freed once. */
    DCG_CHECK(after->parent == guard);
    DCG_CHECK_INT(c_dcg_node_child_count(guard), 2);
    DCG_CHECK(c_dcg_node_child_by_condition(guard, DCG_TRUE_CONDITION) == &breakpoint->base);
    DCG_CHECK(c_dcg_node_child_by_condition(guard, DCG_FALSE_CONDITION) == after);

    /*
     * NOT traced: the breakpoint's child list points at `after`, and `after`'s
     * sibling chain belongs to the guard's list - so a walk that starts at the
     * breakpoint runs on into the guard's children and reaches the breakpoint
     * again. A joined node stands in two parents' lists, and `next_sibling` can
     * only describe one of them; a renderer cannot follow it. The queries above
     * are the contract; the tree is not printable until a join has a link of
     * its own rather than a borrowed child entry.
     */

    DCG_CHECK(c_dcg_node_validate(root, NULL));

    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, after), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, guard), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, root), DCG_OK);

    c_dcg_lgm_dealloc(&mgr); /* a stack manager: the clean half, not the block's */
    /* root, guard, the arm the guard still holds, the breakpoint, and after
     * with the two arms its own enter reserved */
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 6);
    c_dcg_logic_group_free(group);
}

static void test_get_breakpoint(void) {
    /* The finder answers with the break a build left behind, and with nothing
     * once that break has been resumed into - which is the same test the capi
     * makes by looking for a breakpoint among the leaves. */
    dcg_logic_group_manager mgr;
    dcg_logic_group*        group = c_dcg_logic_group_new(DCG_LG_BASE, "find_bp", NULL);
    dcg_node*               root  = dcg_t_node_root("Entry Point");
    dcg_node*               guard = dcg_t_node_plain("guard");

    (void) dcg_t_lgm(&mgr);

    DCG_CHECK(c_dcg_node_get_breakpoint(root) == NULL); /* nothing built yet */
    DCG_CHECK(c_dcg_node_get_breakpoint(NULL) == NULL); /* NULL-safe */

    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, root), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, guard), DCG_OK);
    DCG_CHECK(c_dcg_node_get_breakpoint(root) == NULL); /* still none */

    DCG_CHECK_INT(c_dcg_lgm_enter_group(&mgr, group), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_break_inspection(&mgr, group), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_exit_group(&mgr, group), DCG_OK);

    dcg_breakpoint_node* breakpoint = (dcg_breakpoint_node*) c_dcg_node_child_by_condition(guard, DCG_TRUE_CONDITION);
    DCG_CHECK(breakpoint != NULL);
    DCG_CHECK(c_dcg_node_get_breakpoint(root) == breakpoint);

    /* Resumed: it is where a branch CONTINUED, not where it stopped. */
    dcg_node* after = dcg_t_node_plain("after");
    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, after), DCG_OK);
    DCG_CHECK(c_dcg_node_get_breakpoint(root) == NULL);

    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, after), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, guard), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, root), DCG_OK);

    c_dcg_lgm_dealloc(&mgr);
    c_dcg_node_teardown_root(root);
    c_dcg_logic_group_free(group);
}

static void test_breakpoint_enter(void) {
    /* Entering a breakpoint is the OTHER way to resume: the build goes inside it,
     * and what it builds is what the breakpoint resumes into. Taking it over
     * means the manager stops connecting it, and the one arm it opens is an
     * ELSE - "the branch carried on here" rather than a value to compare. */
    dcg_logic_group_manager mgr;
    dcg_logic_group*        group = c_dcg_logic_group_new(DCG_LG_BASE, "bp_enter", NULL);
    dcg_node*               root  = dcg_t_node_root("Entry Point");
    dcg_node*               guard = dcg_t_node_plain("guard");

    (void) dcg_t_lgm(&mgr);

    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, root), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, guard), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_enter_group(&mgr, group), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_break_inspection(&mgr, group), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_exit_group(&mgr, group), DCG_OK);

    dcg_breakpoint_node* breakpoint = (dcg_breakpoint_node*) c_dcg_node_child_by_condition(guard, DCG_TRUE_CONDITION);
    DCG_CHECK(breakpoint != NULL);
    DCG_CHECK(breakpoint->await_connection);
    DCG_CHECK_INT(c_dcg_lgm_breakpoint_count(&mgr), 1);

    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, &breakpoint->base), DCG_OK);

    /* Taken over: not waiting, not queued, and its arm is open. */
    DCG_CHECK(!breakpoint->await_connection);
    DCG_CHECK_INT(c_dcg_lgm_breakpoint_count(&mgr), 0);
    DCG_CHECK_INT(c_dcg_node_child_count(&breakpoint->base), 1);
    DCG_CHECK_INT(c_dcg_node_type_arity(DCG_NODE_BREAKPOINT), 0); /* the arm is a design, not an arity */

    dcg_node* arm = c_dcg_node_first_child(&breakpoint->base);
    DCG_CHECK(arm != NULL);
    DCG_CHECK(c_dcg_condition_is_else(arm->condition_to_parent));

    /* The build inside it is what it resumes into. */
    dcg_node* resumed = dcg_t_node_plain("resumed");
    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, resumed), DCG_OK);
    DCG_CHECK(c_dcg_node_first_child(&breakpoint->base) == resumed);
    DCG_CHECK(resumed->parent == &breakpoint->base);

    /* A breakpoint resumes into ONE node: entering it again is refused rather
     * than given a second arm. */
    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, &breakpoint->base), DCG_ERR_BUSY);

    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, resumed), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, &breakpoint->base), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, guard), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, root), DCG_OK);

    c_dcg_lgm_dealloc(&mgr);
    /* root, guard, the arm the guard's own exit filled, the breakpoint on the
     * guard's other arm, resumed, and the two arms resumed's own enter reserved.
     * The breakpoint brings none of its own: leaving it fills no arm, because an
     * ELSE arm is not a fallback waiting for a TRUE one. */
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 7);
    c_dcg_logic_group_free(group);
}

static void test_resolve_breakpoint(void) {
    /* The bake's half of a break: the scaffolding comes down and the branch it
     * carried on into takes its place - and its arm, because where the branch
     * goes did not change when the scaffolding did. */
    dcg_logic_group_manager mgr;
    dcg_logic_group*        group = c_dcg_logic_group_new(DCG_LG_BASE, "resolve", NULL);
    dcg_node*               root  = dcg_t_node_root("Entry Point");
    dcg_node*               guard = dcg_t_node_plain("guard");

    (void) dcg_t_lgm(&mgr);

    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, root), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, guard), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_enter_group(&mgr, group), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_break_inspection(&mgr, group), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_exit_group(&mgr, group), DCG_OK);

    dcg_node* breakpoint = c_dcg_node_child_by_condition(guard, DCG_TRUE_CONDITION);
    DCG_CHECK(breakpoint != NULL);

    /* Only a breakpoint resolves, and only one that carried on: a break with
     * nothing behind it has nothing to splice in. */
    DCG_CHECK_INT(c_dcg_node_resolve_breakpoint(NULL), DCG_ERR_TYPE);
    DCG_CHECK_INT(c_dcg_node_resolve_breakpoint(guard), DCG_ERR_TYPE);
    DCG_CHECK_INT(c_dcg_node_resolve_breakpoint(breakpoint), DCG_ERR_UNRESOLVED);
    DCG_CHECK(breakpoint->parent == guard); /* refused: it did not move */

    /* Carry the branch on from inside it, the way a build does. */
    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, breakpoint), DCG_OK);
    dcg_node* continuation = dcg_t_node_plain("continuation");
    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, continuation), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, continuation), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, breakpoint), DCG_OK);

    DCG_CHECK_INT(c_dcg_node_resolve_breakpoint(breakpoint), DCG_OK);


    /* The continuation stands where the break did, on the arm the break held. */
    DCG_CHECK(continuation->parent == guard);
    DCG_CHECK(c_dcg_node_child_by_condition(guard, DCG_TRUE_CONDITION) == continuation);
    DCG_CHECK(c_dcg_condition_is_true(continuation->condition_to_parent));

    /* The breakpoint is DETACHED, not freed: the caller that built it still
     * holds the block, and freeing it here would leave that handle dangling. */
    DCG_CHECK(breakpoint->parent == NULL);
    DCG_CHECK(c_dcg_node_child_by_condition(guard, DCG_TRUE_CONDITION) != breakpoint);
    DCG_CHECK(!(breakpoint->flags & DCG_NODE_FLAG_VISITED)); /* the walks must not reach it */

    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, guard), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, root), DCG_OK);

    c_dcg_lgm_dealloc(&mgr);
    /* root, guard, the arm the guard still holds, the continuation, and the two
     * arms the continuation's own enter reserved */
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 6);
    c_dcg_node_free_generic(breakpoint); /* detached: the builder's to release */
    c_dcg_logic_group_free(group);
}

static void test_break_from_another_group(void) {
    /* A breakpoint only waits for the group it breaks out of: leaving a group
     * it does not name leaves it queued. */
    dcg_logic_group_manager mgr;
    dcg_logic_group*        open  = c_dcg_logic_group_new(DCG_LG_BASE, "open", NULL);
    dcg_logic_group*        other = c_dcg_logic_group_new(DCG_LG_BASE, "other", NULL);
    dcg_node*               root  = dcg_t_node_root("Entry Point");
    dcg_node*               guard = dcg_t_node_plain("guard");

    (void) dcg_t_lgm(&mgr);

    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, root), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, guard), DCG_OK);

    DCG_CHECK_INT(c_dcg_lgm_enter_group(&mgr, open), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_enter_group(&mgr, other), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_break_inspection(&mgr, open), DCG_OK);

    dcg_breakpoint_node* breakpoint = (dcg_breakpoint_node*) c_dcg_node_child_by_condition(guard, DCG_TRUE_CONDITION);

    /* Leaving the inner group is not the one it breaks from. */
    DCG_CHECK_INT(c_dcg_lgm_exit_group(&mgr, other), DCG_OK);
    DCG_CHECK(!breakpoint->await_connection);

    DCG_CHECK_INT(c_dcg_lgm_exit_group(&mgr, open), DCG_OK);
    DCG_CHECK(breakpoint->await_connection);

    c_dcg_lgm_dealloc(&mgr); /* a stack manager: the clean half, not the block's */
    /* root, guard, the arm the guard still holds, and the breakpoint */
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4);
    c_dcg_logic_group_free(open);
    c_dcg_logic_group_free(other);
}

static void test_shelve(void) {
    /* A sub-graph build puts the frames around it away and gets empty stacks;
     * unshelving hands them back whole, modes included. */
    dcg_logic_group_manager mgr;
    dcg_logic_group*        outer = c_dcg_logic_group_new(DCG_LG_BASE, "outer", NULL);
    dcg_logic_group*        inner = c_dcg_logic_group_new(DCG_LG_BASE, "inner", NULL);
    dcg_node*               root  = dcg_t_node_root("Entry Point");

    (void) dcg_t_lgm(&mgr);

    DCG_CHECK_INT(c_dcg_lgm_shelve(&mgr), DCG_OK); /* nothing to shelve is fine */
    DCG_CHECK_INT(c_dcg_lgm_unshelve(&mgr), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_unshelve(&mgr), DCG_ERR_NOT_FOUND);

    DCG_CHECK_INT(c_dcg_lgm_enter_group(&mgr, outer), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, root), DCG_OK);
    mgr.inspection_mode = true;

    /* Entering the root shelved already - it opened a context of its own - so
     * this is the second shelf, not the first. */
    DCG_CHECK_INT(mgr.n_shelved, 1);
    DCG_CHECK_INT(c_dcg_lgm_shelve(&mgr), DCG_OK);
    DCG_CHECK(c_dcg_lgm_active_group(&mgr) == NULL); /* the manager is left empty */
    DCG_CHECK(c_dcg_lgm_active_node(&mgr) == NULL);
    DCG_CHECK_INT(mgr.n_shelved, 2);
    dcg_t_trace_lgm("shelved", &mgr);

    /* The shelved build is genuinely out of the way: a sub-graph runs on the
     * same manager without seeing any of it. */
    DCG_CHECK_INT(c_dcg_lgm_enter_group(&mgr, inner), DCG_OK);
    DCG_CHECK(c_dcg_lgm_active_group(&mgr) == inner);
    DCG_CHECK(inner->parent == NULL); /* it nests in nothing - the outer group is away */

    /* Shelving nests, and unshelving unwinds innermost first. */
    DCG_CHECK_INT(c_dcg_lgm_shelve(&mgr), DCG_OK);
    DCG_CHECK_INT(mgr.n_shelved, 3);
    DCG_CHECK_INT(c_dcg_lgm_unshelve(&mgr), DCG_OK);
    DCG_CHECK(c_dcg_lgm_active_group(&mgr) == inner);

    DCG_CHECK_INT(c_dcg_lgm_unshelve(&mgr), DCG_OK);
    DCG_CHECK(c_dcg_lgm_active_group(&mgr) == NULL); /* this shelf was the root's own */

    DCG_CHECK_INT(c_dcg_lgm_unshelve(&mgr), DCG_OK);
    DCG_CHECK(c_dcg_lgm_active_group(&mgr) == outer);
    DCG_CHECK(c_dcg_lgm_active_node(&mgr) == root);
    /* The modes travelled with the frames, so this is the one the ROOT's own
     * shelf carries - the mode from before it was entered, not the inspection
     * mode it set for its own context. */
    DCG_CHECK(!mgr.inspection_mode);

    c_dcg_lgm_dealloc(&mgr); /* a stack manager: the clean half, not the block's */
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 2); /* the root and the arm its enter reserved */
    c_dcg_logic_group_free(outer);
    c_dcg_logic_group_free(inner);
}

static void test_mapping_is_a_group(void) {
    /* The port's whole point, checked from the manager's side: a mapping is a
     * logic group, so it opens, nests, labels and breaks like any other - and
     * the entries it holds are read through the same scope it opens. */
    dcg_logic_group_manager mgr;
    dcg_mapping_lgroup*     state = c_dcg_mapping_lgroup_new("state", 2, NULL);
    dcg_node*               root  = dcg_t_node_root("Entry Point");

    (void) dcg_t_lgm(&mgr);

    DCG_CHECK_INT(c_dcg_lgm_register(&mgr, &state->base), DCG_OK);
    DCG_CHECK(c_dcg_lgm_find(&mgr, "state", 5) == &state->base);

    DCG_CHECK_INT(c_dcg_lgm_enter_group(&mgr, &state->base), DCG_OK);
    DCG_CHECK(c_dcg_lgm_active_group(&mgr) == &state->base);

    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_int(state, "exposure", 8, 1), DCG_OK);
    dcg_variable_node* var = c_dcg_mapping_lgroup_get_node(state, "exposure", 8, NULL);
    DCG_CHECK(var != NULL);
    DCG_CHECK(var->logic_group == &state->base);

    /* Entering a root shelves, so the group it is entered from is out of scope
     * - and the entry would go unlabelled. `inherit_contexts` is what carries
     * the groups across the shelve, which is exactly what this reads through. */
    ((dcg_root_node*) root)->inherit_contexts = true;

    DCG_CHECK_INT(c_dcg_lgm_enter_node(&mgr, root), DCG_OK);

    /* Entering it labelled it: a node entered inside a group is a member of it,
     * and saying so is the entering's business rather than the constructor's -
     * a node built outside any group belongs to none. */
    DCG_CHECK(c_dcg_node_has_label(root, "state"));

    /* And the explicit call adds nothing on top: a name already on the node is
     * left alone, so labelling twice is the same as labelling once. */
    DCG_CHECK_INT(c_dcg_lgm_label_node(&mgr, root), 0);

    DCG_CHECK_INT(c_dcg_lgm_exit_node(&mgr, root), DCG_OK);
    DCG_CHECK_INT(c_dcg_lgm_exit_group(&mgr, &state->base), DCG_OK);

    c_dcg_node_free_var(var);
    c_dcg_lgm_dealloc(&mgr); /* a stack manager: the clean half, not the block's */
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 2); /* the root and the arm its enter reserved */
    c_dcg_mapping_lgroup_free(state);
}

int main(void) {
    (void) printf("test_c_logic_group\n");
    DCG_RUN(test_group_lifecycle);
    DCG_RUN(test_manager_lifecycle);
    DCG_RUN(test_registry);
    DCG_RUN(test_group_stack);
    DCG_RUN(test_node_stack);
    DCG_RUN(test_labels);
    DCG_RUN(test_break_inspection);
    DCG_RUN(test_break_resume);
    DCG_RUN(test_get_breakpoint);
    DCG_RUN(test_breakpoint_enter);
    DCG_RUN(test_resolve_breakpoint);
    DCG_RUN(test_break_from_another_group);
    DCG_RUN(test_shelve);
    DCG_RUN(test_mapping_is_a_group);
    DCG_SUMMARY("test_c_logic_group");
    return dcg_test_failures == 0 ? 0 : 1;
}
