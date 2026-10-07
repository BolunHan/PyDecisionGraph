/*
 * The bake protocol: what a graph is owed before it is evaluated.
 *
 * The other suites build graphs and evaluate them. This one does to a graph what
 * a caller does LAST: it asks the root to verify the graph, lock it and prepare
 * it. What it asserts is therefore of three kinds, and they are kept apart
 * deliberately:
 *
 *   - what the pass REFUSES. A graph missing an operand, holding an operator its
 *     arity does not apply, or carrying a node with no evaluation at all is a
 *     graph that cannot be walked - and the point of the pass is that it says so
 *     once, up front, instead of one evaluation at a time;
 *   - what the pass CHANGES. A locked node takes no child, a sealed store takes
 *     no entry, and the record a walk fills has its room before the walk runs;
 *   - what the pass LEAVES. A graph that fails is exactly as it was found, a
 *     second bake locks nothing, and a graph that was baked still decides.
 *
 * The two edges a graph has are both exercised, because both are the graph: the
 * tree's branches, and the operands an operator's rule runs - which are not
 * children of anything, and which a walk that followed only children would walk
 * straight past.
 */

#include <string.h>

#include <decision_graph/decision_tree/bake/c_bake.h>
#include <decision_graph/decision_tree/bake/c_collections.h>

#include "test_util.h"

// ========== Fixtures ==========

static void t_free(dcg_node* node) {
    c_dcg_node_free_generic(node);
}

/** Attach a child, reporting the edge the layer refused. */
static void t_attach(dcg_node* parent, dcg_node* child, const dcg_node_edge_condition* condition) {
    int ret_code = c_dcg_node_append(parent, child, condition);
    if (ret_code != DCG_OK) {
        (void) fprintf(stderr, "  attach refused: %s\n", c_dcg_ret_code_name(ret_code));
        dcg_test_failures++;
    }
}

/** A binary node over a node the caller keeps alive, and one literal. */
static dcg_node* t_binary_of_num(dcg_op_code op, dcg_node* lhs, double y) {
    dcg_constant_node*   rhs  = c_dcg_node_new_const_double(y, NULL);
    dcg_expression_node* node = c_dcg_node_new_expr_binary(op, lhs, &rhs->base, NULL);
    c_ap_decref(&rhs->base);
    return node ? &node->base : NULL;
}

/** A store with one entry reserved, and the read that names it. */
static dcg_variable_node* t_read(dcg_mapping_lgroup* store, const char* key) {
    dcg_var_t* slot = NULL;
    if (c_dcg_mapping_lgroup_get_create_slot(store, key, strlen(key), NULL, &slot) != DCG_OK) return NULL;
    return c_dcg_mapping_lgroup_get_node(store, key, strlen(key), NULL);
}

/*
 * The tree every case here starts from, in the shape a build produces:
 *
 *   Entry
 *   └── signal > 0            the branch: a read compared against a literal
 *       ├── LongAction        taken when it holds
 *       └── CancelAction      taken when it does not
 *
 * `read` and `literal` are operands of the branch and children of nothing: they
 * are reached through the node that reads them, not through the tree.
 */
typedef struct t_tree {
    dcg_root_node*      root;
    dcg_mapping_lgroup* store;
    dcg_variable_node*  read;
    dcg_node*           branch;
    dcg_node*           yes;
    dcg_node*           no;
} t_tree;

static t_tree t_tree_build(void) {
    t_tree tree;

    tree.store  = dcg_t_mapping_lgroup(4, "decide");
    tree.read   = t_read(tree.store, "signal");
    tree.root   = c_dcg_node_new_root("Entry", NULL);
    tree.branch = t_binary_of_num(DCG_OP_GT, &tree.read->base, 0.0);
    tree.yes    = dcg_t_node_action(DCG_NODE_LONGACTION, "LongAction");
    tree.no     = dcg_t_node_action(DCG_NODE_CANCELACTION, "CancelAction");

    t_attach(&tree.root->base, tree.branch, DCG_NO_CONDITION);
    t_attach(tree.branch, tree.yes, DCG_TRUE_CONDITION);
    t_attach(tree.branch, tree.no, DCG_FALSE_CONDITION);
    return tree;
}

static void t_tree_free(t_tree* tree) {
    c_dcg_node_teardown_root(&tree->root->base); /* the tree, and the hold its branch has on the read */
    c_dcg_mapping_lgroup_free(tree->store);
}

/** Trace a bake report: what the pass found, and what it affected. */
static void t_trace_report(const char* what, const dcg_bake_report* report) {
    (void) printf(
        "    %-26s %-9s node=%-9s errors=%zu nodes=%zu depth=%zu locked=%zu sealed=%zu capacity=%zu\n",
        what ? what : "",
        c_dcg_ret_code_name(report->code),
        report->node ? c_dcg_node_type_name(report->node->ntype) : "(none)",
        report->errors,
        report->nodes,
        report->depth,
        report->locked,
        report->sealed,
        report->capacity
    );
}

// ========== What the pass accepts ==========

static void test_a_well_formed_graph_is_baked(void) {
    t_tree tree = t_tree_build();

    dcg_bake_report report;
    DCG_CHECK_INT(c_dcg_root_node_bake(tree.root, NULL, &report), DCG_OK);
    t_trace_report("baked", &report);

    /* Every node the pass walked is locked - the branches, the operands they
     * read, and the root itself. */
    DCG_CHECK_INT(report.code, DCG_OK);
    DCG_CHECK_INT(report.errors, 0);
    DCG_CHECK_INT(report.locked, 6); /* root, branch, two actions, the read, the literal */
    DCG_CHECK_INT(report.nodes, 6);
    DCG_CHECK_INT(report.depth, 2);
    DCG_CHECK(report.node == NULL);

    DCG_CHECK(report.capacity == c_dcg_node_height(&tree.root->base) + 1);
    DCG_CHECK(tree.root->eval_path.node != NULL);
    DCG_CHECK(tree.root->eval_path.eval_val != NULL);

    t_tree_free(&tree);
}

static void test_the_walk_reaches_the_operands_the_tree_cannot(void) {
    t_tree tree = t_tree_build();

    dcg_bake_report report;
    DCG_CHECK_INT(c_dcg_root_node_bake(tree.root, NULL, &report), DCG_OK);

    /* The read is an operand of the branch and a child of nothing. If the walk
     * followed children alone it would never arrive - and the store behind it
     * would go on growing under a graph that reads it. */
    DCG_CHECK(tree.read->base.flags & DCG_NODE_FLAG_FROZEN);
    DCG_CHECK(tree.store->frozen);
    DCG_CHECK_INT(report.sealed, 1);

    /* The literal the branch was built over: reached the same way. */
    dcg_expression_node* branch = (dcg_expression_node*) tree.branch;
    DCG_CHECK(branch->components[1]->flags & DCG_NODE_FLAG_FROZEN);

    t_tree_free(&tree);
}

static void test_a_shared_operand_is_walked_once(void) {
    /* `(shared + shared) > 0`: the operand both slots of the sum name is ONE
     * node, and the count is a count of nodes - which a walk that counted edges
     * would get wrong. */
    dcg_node*            shared = dcg_t_node_int("5", 5);
    dcg_node*            sum    = t_binary_of_num(DCG_OP_ADD, shared, 1.0);
    dcg_expression_node* expr   = (dcg_expression_node*) sum;
    DCG_CHECK_INT(c_dcg_node_expr_bind(expr, 1, shared), DCG_OK); /* the same node in both slots */

    dcg_root_node* root   = c_dcg_node_new_root("Entry", NULL);
    dcg_node*      branch = t_binary_of_num(DCG_OP_GT, sum, 0.0);
    t_attach(&root->base, branch, DCG_NO_CONDITION);
    t_attach(branch, dcg_t_node_action(DCG_NODE_LONGACTION, "LongAction"), DCG_TRUE_CONDITION);
    t_attach(branch, dcg_t_node_action(DCG_NODE_CANCELACTION, "CancelAction"), DCG_FALSE_CONDITION);

    dcg_bake_report report;
    DCG_CHECK_INT(c_dcg_root_node_bake(root, NULL, &report), DCG_OK);
    DCG_CHECK_INT(report.nodes, 7); /* root, branch, two arms, the sum, and the operand both slots name */
    DCG_CHECK_INT(report.locked, 7);

    /* The tree first, then the node it held: a nested expression gives its
     * operands back when IT is freed, so it is freed from the handle that owns
     * it rather than left to the last reference going. */
    c_dcg_node_teardown_root(&root->base);
    t_free(sum);
    t_free(shared);
}

static void test_an_operand_that_leads_back_to_its_own_node_terminates(void) {
    /* A node bound as its own operand: the walk marks what it reaches and stops
     * at what it has already reached, so a graph that is not a tree is walked to
     * its end rather than forever. */
    dcg_expression_node* node = c_dcg_node_new_expr(2, DCG_NODE_BINARY, NULL);
    DCG_CHECK_INT(c_dcg_node_expr_set_op(node, DCG_OP_ADD), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_expr_bind(node, 0, &node->base), DCG_OK); /* itself */

    dcg_constant_node* one = c_dcg_node_new_const_int(1, NULL);
    DCG_CHECK_INT(c_dcg_node_expr_bind(node, 1, &one->base), DCG_OK);
    c_ap_decref(&one->base);

    dcg_root_node* root   = c_dcg_node_new_root("Entry", NULL);
    dcg_node*      branch = t_binary_of_num(DCG_OP_GT, &node->base, 0.0);
    t_attach(&root->base, branch, DCG_NO_CONDITION);
    t_attach(branch, dcg_t_node_action(DCG_NODE_LONGACTION, "LongAction"), DCG_TRUE_CONDITION);
    t_attach(branch, dcg_t_node_action(DCG_NODE_CANCELACTION, "CancelAction"), DCG_FALSE_CONDITION);

    dcg_bake_report report;
    DCG_CHECK_INT(c_dcg_root_node_bake(root, NULL, &report), DCG_OK);
    DCG_CHECK_INT(report.nodes, 7); /* the self-bound node is one node, reached once */
    DCG_CHECK(report.locked >= 7);
    DCG_CHECK(node->base.flags & DCG_NODE_FLAG_FROZEN);

    c_dcg_node_teardown_root(&root->base);
    t_free(&node->base);
}

static void test_a_baked_graph_still_decides(void) {
    t_tree tree = t_tree_build();

    DCG_CHECK_INT(c_dcg_root_node_bake(tree.root, NULL, NULL), DCG_OK); /* the report is the caller's to want */

    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(tree.store, "signal", 6, 2.5), DCG_OK);
    DCG_CHECK_INT(c_dcg_root_node_eval(tree.root), DCG_OK);
    DCG_CHECK(tree.root->eval_path.leaf == tree.yes);

    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(tree.store, "signal", 6, -2.5), DCG_OK);
    DCG_CHECK_INT(c_dcg_root_node_eval(tree.root), DCG_OK);
    DCG_CHECK(tree.root->eval_path.leaf == tree.no);

    t_tree_free(&tree);
}

// ========== What the pass changes ==========

static void test_the_lockdown_refuses_what_it_promised_to(void) {
    t_tree tree = t_tree_build();
    DCG_CHECK_INT(c_dcg_root_node_bake(tree.root, NULL, NULL), DCG_OK);

    /* The structure of a baked graph is the structure it was baked with. */
    dcg_node* late = dcg_t_node_action(DCG_NODE_CLEARACTION, "ClearAction");
    DCG_CHECK_INT(c_dcg_node_append(tree.branch, late, DCG_ELSE_CONDITION), DCG_ERR_BUSY);
    DCG_CHECK_INT(c_dcg_node_detach(tree.yes), DCG_ERR_BUSY);
    DCG_CHECK_INT(c_dcg_node_remove(tree.yes), DCG_ERR_BUSY);
    DCG_CHECK_INT(c_dcg_node_clear_children(tree.branch), 0);

    /* ...and the two writers an expression has of its own: the operands it runs
     * and the operator it applies. */
    dcg_expression_node* expr   = (dcg_expression_node*) tree.branch;
    dcg_node*            other  = dcg_t_node_int("7", 7);
    DCG_CHECK_INT(c_dcg_node_expr_bind(expr, 0, other), DCG_ERR_BUSY);
    DCG_CHECK_INT(c_dcg_node_expr_set_op(expr, DCG_OP_LT), DCG_ERR_BUSY);

    t_free(late);
    t_free(other);
    t_tree_free(&tree);
}

static void test_the_store_a_read_names_is_sealed(void) {
    t_tree tree = t_tree_build();
    DCG_CHECK_INT(c_dcg_root_node_bake(tree.root, NULL, NULL), DCG_OK);

    /* No entry appears in a sealed store... */
    dcg_var_t* slot = NULL;
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_create_slot(tree.store, "late", 4, NULL, &slot), DCG_ERR_BUSY);
    DCG_CHECK(slot == NULL);

    /* ...and the entries it has go on being written, which is the whole point of
     * baking a graph once and feeding it many times. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(tree.store, "signal", 6, 1.0), DCG_OK);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(tree.store, "signal", 6, 2.0), DCG_OK);

    t_tree_free(&tree);
}

static void test_the_record_is_made_before_the_walk_needs_it(void) {
    t_tree tree = t_tree_build();

    DCG_CHECK(tree.root->eval_path.node == NULL); /* nothing has been walked yet */
    DCG_CHECK_INT(c_dcg_root_node_bake(tree.root, NULL, NULL), DCG_OK);

    /* The room is what a walk from this root can need: one entry per level. */
    size_t capacity = tree.root->eval_path.capacity;
    DCG_CHECK_INT(capacity, c_dcg_node_height(&tree.root->base) + 1);

    /* A baked graph's walk makes no room of its own, however deep it goes. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(tree.store, "signal", 6, 1.0), DCG_OK);
    DCG_CHECK_INT(c_dcg_root_node_eval(tree.root), DCG_OK);
    DCG_CHECK(tree.root->eval_path.capacity == capacity);
    DCG_CHECK_INT(tree.root->eval_path.n_nodes, 3); /* the root, the branch, the leaf */

    /* A record that already has room is left as it is, and reported with it. */
    dcg_bake_report report;
    DCG_CHECK_INT(c_dcg_root_node_bake(tree.root, NULL, &report), DCG_OK);
    DCG_CHECK(report.capacity == capacity);

    t_tree_free(&tree);
}

static void test_a_second_bake_locks_nothing(void) {
    t_tree tree = t_tree_build();

    dcg_bake_report first;
    DCG_CHECK_INT(c_dcg_root_node_bake(tree.root, NULL, &first), DCG_OK);
    DCG_CHECK(first.locked > 0 && first.sealed > 0);

    dcg_bake_report second;
    DCG_CHECK_INT(c_dcg_root_node_bake(tree.root, NULL, &second), DCG_OK);
    DCG_CHECK_INT(second.locked, 0);
    DCG_CHECK_INT(second.sealed, 0);
    DCG_CHECK(second.nodes == first.nodes);
    t_trace_report("re-baked", &second);

    t_tree_free(&tree);
}

static void test_a_validate_only_bake_changes_nothing(void) {
    t_tree tree = t_tree_build();

    dcg_bake_input input;
    c_dcg_bake_input_init(&input);
    input.flags = DCG_BAKE_FLAG_VALIDATE_ONLY;

    dcg_bake_report report;
    DCG_CHECK_INT(c_dcg_root_node_bake(tree.root, &input, &report), DCG_OK);
    DCG_CHECK_INT(report.locked, 0);
    DCG_CHECK_INT(report.sealed, 0);
    DCG_CHECK_INT(report.capacity, 0);
    DCG_CHECK(report.nodes == 6); /* the verdict is the whole of what it was asked for */

    /* Nothing is locked, nothing is sealed, and no scratch is left behind. */
    DCG_CHECK(!(tree.root->base.flags & DCG_NODE_FLAG_FROZEN));
    DCG_CHECK(!(tree.read->base.flags & DCG_NODE_FLAG_FROZEN));
    DCG_CHECK(!tree.store->frozen);
    DCG_CHECK(tree.root->eval_path.node == NULL);

    DCG_CHECK(!(tree.root->base.flags & DCG_NODE_FLAG_VISITED));
    DCG_CHECK(!(tree.branch->flags & DCG_NODE_FLAG_VISITED));
    DCG_CHECK(!(tree.read->base.flags & DCG_NODE_FLAG_VISITED));

    /* ...so the graph goes on being built, and bakes afterwards. */
    DCG_CHECK_INT(c_dcg_node_append(tree.branch, dcg_t_node_action(DCG_NODE_NOACTION, "NoAction"), DCG_ELSE_CONDITION), DCG_OK);
    t_trace_report("validate only", &report);

    t_tree_free(&tree);
}

// ========== What the pass refuses ==========

static void test_an_unbound_operand_is_refused(void) {
    /* `(kept + nothing) > 0`: an operand slot of the sum has no component behind
     * it, which is the read its rule would do with nothing to read. */
    dcg_node* sum  = t_binary_of_num(DCG_OP_ADD, dcg_t_node_int("1", 1), 2.0);
    dcg_node* kept = ((dcg_expression_node*) sum)->components[0];

    t_free(((dcg_expression_node*) sum)->components[1]);
    ((dcg_expression_node*) sum)->components[1] = NULL;

    dcg_root_node* root   = c_dcg_node_new_root("Entry", NULL);
    dcg_node*      branch = t_binary_of_num(DCG_OP_GT, sum, 0.0);
    t_attach(&root->base, branch, DCG_NO_CONDITION);
    t_attach(branch, dcg_t_node_action(DCG_NODE_LONGACTION, "LongAction"), DCG_TRUE_CONDITION);
    t_attach(branch, dcg_t_node_action(DCG_NODE_CANCELACTION, "CancelAction"), DCG_FALSE_CONDITION);

    dcg_bake_report report;
    DCG_CHECK_INT(c_dcg_root_node_bake(root, NULL, &report), DCG_ERR_UNBOUND);
    t_trace_report("unbound operand", &report);
    DCG_CHECK(report.node == sum); /* the node that would have read it */
    DCG_CHECK_INT(report.errors, 1);

    /* A graph that fails is left exactly as it was found. */
    DCG_CHECK(!(root->base.flags & DCG_NODE_FLAG_FROZEN));
    DCG_CHECK(!(sum->flags & DCG_NODE_FLAG_FROZEN));
    DCG_CHECK(root->eval_path.node == NULL);
    DCG_CHECK(!(root->base.flags & DCG_NODE_FLAG_VISITED));
    DCG_CHECK(!(sum->flags & DCG_NODE_FLAG_VISITED));

    c_dcg_node_teardown_root(&root->base);
    t_free(sum);
    t_free(kept);
}

static void test_an_operator_of_another_arity_is_refused(void) {
    /* A unary node carrying a binary operator: nothing installs a rule for it,
     * so the node would refuse to evaluate every time it was asked. */
    dcg_constant_node*   one  = c_dcg_node_new_const_int(1, NULL);
    dcg_expression_node* node = c_dcg_node_new_expr(1, DCG_NODE_UNARY, NULL);
    DCG_CHECK_INT(c_dcg_node_expr_set_op(node, DCG_OP_ADD), DCG_OK);
    c_dcg_node_expr_bind(node, 0, &one->base);
    c_ap_decref(&one->base); /* the node holds the operand now */

    dcg_root_node* root   = c_dcg_node_new_root("Entry", NULL);
    dcg_node*      branch = t_binary_of_num(DCG_OP_GT, &node->base, 0.0);
    t_attach(&root->base, branch, DCG_NO_CONDITION);
    t_attach(branch, dcg_t_node_action(DCG_NODE_LONGACTION, "LongAction"), DCG_TRUE_CONDITION);
    t_attach(branch, dcg_t_node_action(DCG_NODE_CANCELACTION, "CancelAction"), DCG_FALSE_CONDITION);

    dcg_bake_report report;
    DCG_CHECK_INT(c_dcg_root_node_bake(root, NULL, &report), DCG_ERR_TYPE);
    DCG_CHECK(report.node == &node->base);

    c_dcg_node_teardown_root(&root->base);
    t_free(&node->base);
}

static void test_a_call_is_refused(void) {
    dcg_node* call = dcg_t_node_call("spread");

    dcg_root_node* root = c_dcg_node_new_root("Entry", NULL);
    t_attach(&root->base, call, DCG_NO_CONDITION);

    dcg_bake_report report;
    DCG_CHECK_INT(c_dcg_root_node_bake(root, NULL, &report), DCG_ERR_TYPE);
    DCG_CHECK(report.node == call); /* a callee composed into a repr is not an evaluation */
    t_trace_report("call", &report);

    c_dcg_node_teardown_root(&root->base);
}

static void test_an_operand_array_shorter_than_the_arity_is_refused(void) {
    /* A block with room for one operand, read as a binary: the second slot its
     * rule reads is past the end of the node. The arms are given the count the
     * type's arity asks for, so the graph's own shape is sound and the operand
     * array is the only thing wrong with it. */
    dcg_expression_node* node = c_dcg_node_new_expr(1, DCG_NODE_BINARY, NULL);
    DCG_CHECK_INT(c_dcg_node_expr_set_op(node, DCG_OP_ADD), DCG_OK);

    dcg_constant_node* one = c_dcg_node_new_const_int(1, NULL);
    c_dcg_node_expr_bind(node, 0, &one->base);
    c_ap_decref(&one->base); /* the node holds the operand now */

    dcg_root_node* root   = c_dcg_node_new_root("Entry", NULL);
    dcg_node*      branch = t_binary_of_num(DCG_OP_GT, &node->base, 0.0);
    t_attach(&root->base, branch, DCG_NO_CONDITION);
    t_attach(branch, dcg_t_node_action(DCG_NODE_LONGACTION, "LongAction"), DCG_TRUE_CONDITION);
    t_attach(branch, dcg_t_node_action(DCG_NODE_CANCELACTION, "CancelAction"), DCG_FALSE_CONDITION);

    dcg_bake_report report;
    DCG_CHECK_INT(c_dcg_root_node_bake(root, NULL, &report), DCG_ERR_TYPE);
    DCG_CHECK(report.node == &node->base);

    c_dcg_node_teardown_root(&root->base);
    t_free(&node->base);
}

static void test_a_broken_graph_is_refused_before_anything_is_locked(void) {
    /* A branch whose arm count is not its arity: the structural walk's to refuse. */
    dcg_root_node* root   = c_dcg_node_new_root("Entry", NULL);
    dcg_node*      one    = dcg_t_node_int("1", 1);
    dcg_node*      branch = t_binary_of_num(DCG_OP_GT, one, 0.0);
    dcg_node*      orphan = dcg_t_node_action(DCG_NODE_LONGACTION, "LongAction");
    c_ap_decref(one); /* the branch holds the literal now */

    t_attach(&root->base, branch, DCG_NO_CONDITION);
    t_attach(branch, orphan, DCG_TRUE_CONDITION); /* one arm where two are read */

    dcg_bake_report report;
    DCG_CHECK_INT(c_dcg_root_node_bake(root, NULL, &report), DCG_ERR_TYPE);
    DCG_CHECK_INT(report.errors, 1);
    DCG_CHECK(report.node == branch);
    t_trace_report("half a branch", &report);

    /* The structural half ran first, so the operands were never reached and
     * nothing was locked - the graph is the graph the caller still has. */
    DCG_CHECK(!(root->base.flags & DCG_NODE_FLAG_FROZEN));
    DCG_CHECK(!(branch->flags & DCG_NODE_FLAG_FROZEN));
    DCG_CHECK(root->eval_path.node == NULL);

    c_dcg_node_teardown_root(&root->base);
}

static void test_a_root_is_required_and_a_report_is_optional(void) {
    dcg_bake_report report;
    DCG_CHECK_INT(c_dcg_root_node_bake(NULL, NULL, &report), DCG_ERR_INVALID_ARG);

    /* A caller that wants only the verdict: the report is written to a local of
     * the pass's own, and the code comes back whole. */
    t_tree tree = t_tree_build();
    DCG_CHECK_INT(c_dcg_root_node_bake(tree.root, NULL, NULL), DCG_OK);
    t_tree_free(&tree);
}

static void test_the_input_defaults_to_the_whole_of_a_bake(void) {
    dcg_bake_input input;
    memset(&input, 0xFF, sizeof(input)); /* garbage, so the default has to be written */
    c_dcg_bake_input_init(&input);
    DCG_CHECK_INT((int) input.flags, DCG_BAKE_FLAG_NONE);

    t_tree tree = t_tree_build();
    DCG_CHECK_INT(c_dcg_root_node_bake(tree.root, &input, NULL), DCG_OK);
    DCG_CHECK(tree.store->frozen); /* the default is the whole of a bake, not a dry run */
    t_tree_free(&tree);
}

static void test_a_report_can_be_had_of_its_own(void) {
    /* The other half of the door's API: a caller that wants the answer keeps it,
     * so the report's type carries the two functions every owning type has. */
    dcg_bake_report* report = c_dcg_bake_report_new(NULL);
    DCG_CHECK(report != NULL);
    DCG_CHECK_INT(report->code, DCG_OK); /* a bake that never ran */
    DCG_CHECK(report->node == NULL);
    DCG_CHECK_INT(report->errors, 0);
    DCG_CHECK_INT(report->capacity, 0);

    t_tree tree = t_tree_build();
    DCG_CHECK_INT(c_dcg_root_node_bake(tree.root, NULL, report), DCG_OK);
    DCG_CHECK_INT(report->locked, 6); /* filled in place */

    c_dcg_bake_report_free(report);
    c_dcg_bake_report_free(NULL); /* and releasing nothing is nothing */

    t_tree_free(&tree);
}

// ========== main ==========

int main(void) {
    (void) printf("test_c_bake\n");

    DCG_RUN(test_a_well_formed_graph_is_baked);
    DCG_RUN(test_the_walk_reaches_the_operands_the_tree_cannot);
    DCG_RUN(test_a_shared_operand_is_walked_once);
    DCG_RUN(test_an_operand_that_leads_back_to_its_own_node_terminates);
    DCG_RUN(test_a_baked_graph_still_decides);

    DCG_RUN(test_the_lockdown_refuses_what_it_promised_to);
    DCG_RUN(test_the_store_a_read_names_is_sealed);
    DCG_RUN(test_the_record_is_made_before_the_walk_needs_it);
    DCG_RUN(test_a_second_bake_locks_nothing);
    DCG_RUN(test_a_validate_only_bake_changes_nothing);

    DCG_RUN(test_an_unbound_operand_is_refused);
    DCG_RUN(test_an_operator_of_another_arity_is_refused);
    DCG_RUN(test_a_call_is_refused);
    DCG_RUN(test_an_operand_array_shorter_than_the_arity_is_refused);
    DCG_RUN(test_a_broken_graph_is_refused_before_anything_is_locked);

    DCG_RUN(test_a_root_is_required_and_a_report_is_optional);
    DCG_RUN(test_the_input_defaults_to_the_whole_of_a_bake);
    DCG_RUN(test_a_report_can_be_had_of_its_own);

    DCG_SUMMARY("test_c_bake");
    return dcg_test_failures == 0 ? 0 : 1;
}
