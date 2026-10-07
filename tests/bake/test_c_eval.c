/*
 * The evaluation protocol: what a graph DOES, given what it is.
 *
 * The other suites build graphs and check their shape. This one evaluates them,
 * and what it asserts is the value each node came to hold, the path the walk
 * took to a leaf, and the bookkeeping the walk left on the nodes it visited.
 *
 * An eval path is asserted node by node, because "which leaf was reached" is
 * only the last line of the answer: how the walk got there is what a caller
 * reads the record for, and a path that lands on the right leaf through the
 * wrong branches is a graph that will decide differently on other input.
 *
 * The fixtures build small trees on purpose - each one names the branch order it
 * is checking - and the read tests build the thing a caller builds: an entry the
 * build reserved, a node reading it, the value arriving afterwards.
 */

#include <string.h>

#include <decision_graph/decision_tree/bake/c_collections.h>
#include <decision_graph/decision_tree/bake/c_eval.h>

#include "test_util.h"

// ========== Node fixtures ==========

/** Free a node of any type: the family free is the dispatcher's to choose. */
static void t_free(dcg_node* node) {
    c_dcg_node_free_generic(node);
}

static dcg_node* t_num(double value) {
    return dcg_t_node_double(NULL, value);
}

/** A binary node over two literals, which the binding folds into the slots. */
static dcg_node* t_binary(dcg_op_code op, double x, double y) {
    dcg_constant_node*   lhs  = c_dcg_node_new_const_double(x, NULL);
    dcg_constant_node*   rhs  = c_dcg_node_new_const_double(y, NULL);
    dcg_expression_node* node = c_dcg_node_new_expr_binary(op, &lhs->base, &rhs->base, NULL);
    c_ap_decref(&lhs->base); /* the expression holds the operand now: this reference goes back */
    c_ap_decref(&rhs->base);
    return node ? &node->base : NULL;
}

/** The same, over two whole numbers - so the result is one as well. */
static dcg_node* t_binary_int(dcg_op_code op, ssize_t x, ssize_t y) {
    dcg_constant_node*   lhs  = c_dcg_node_new_const_int(x, NULL);
    dcg_constant_node*   rhs  = c_dcg_node_new_const_int(y, NULL);
    dcg_expression_node* node = c_dcg_node_new_expr_binary(op, &lhs->base, &rhs->base, NULL);
    c_ap_decref(&lhs->base);
    c_ap_decref(&rhs->base);
    return node ? &node->base : NULL;
}

/** A binary node over a node the caller keeps alive, and one literal. */
static dcg_node* t_binary_of_num(dcg_op_code op, dcg_node* lhs, double y) {
    dcg_constant_node*   rhs  = c_dcg_node_new_const_double(y, NULL);
    dcg_expression_node* node = c_dcg_node_new_expr_binary(op, lhs, &rhs->base, NULL);
    c_ap_decref(&rhs->base);
    return node ? &node->base : NULL;
}

static dcg_node* t_unary(dcg_op_code op, double x) {
    dcg_constant_node*   src  = c_dcg_node_new_const_double(x, NULL);
    dcg_expression_node* node = c_dcg_node_new_expr_unary(op, &src->base, NULL);
    c_ap_decref(&src->base);
    return node ? &node->base : NULL;
}

/** A unary node over a string: the operand the arithmetic operators refuse. */
static dcg_node* t_unary_string(dcg_op_code op, const char* text) {
    dcg_constant_node*   src  = c_dcg_node_new_const_string(text, NULL);
    dcg_expression_node* node = c_dcg_node_new_expr_unary(op, &src->base, NULL);
    c_ap_decref(&src->base);
    return node ? &node->base : NULL;
}

/** A ternary over three nodes the caller keeps alive. */
static dcg_node* t_ternary(dcg_node* condition, dcg_node* then_arm, dcg_node* else_arm) {
    dcg_expression_node* node = c_dcg_node_new_expr_ternary(DCG_OP_NONE, condition, then_arm, else_arm, NULL);
    return node ? &node->base : NULL;
}

/** Attach a child, reporting the edge the layer refused. */
static void t_attach(dcg_node* parent, dcg_node* child, const dcg_node_edge_condition* condition) {
    int ret_code = c_dcg_node_append(parent, child, condition);
    if (ret_code != DCG_OK) {
        (void) fprintf(stderr, "  attach refused: %s\n", c_dcg_ret_code_name(ret_code));
        dcg_test_failures++;
    }
    dcg_test_checks++;
}

/** Trace one evaluated node: where it sat, how it ended, and what it holds. */
static void t_trace_eval(const char* what, const dcg_node* node) {
    char buf[DCG_VAR_STRING_MAXLEN];
    (void) c_dcg_var_format(&node->out, buf, sizeof(buf));
    (void) printf("    %-22s %-11s stage=0x%02x err=%-9s depth=%zu visits=%zu out=%s\n", what ? what : "", c_dcg_node_type_name(node->ntype), node->eval_ctx.stage,
                  c_dcg_ret_code_name(node->eval_ctx.err_code), node->eval_ctx.depth, node->eval_ctx.visits, buf);
}

/** Trace a whole eval path: every node in the order the walk reached it. */
static void t_trace_path(const char* what, const dcg_node_eval_path* path) {
    (void) printf("    %-22s code=%-9s leaf=%s nodes=%zu\n", what ? what : "", c_dcg_ret_code_name(path->code), path->leaf ? c_dcg_node_type_name(path->leaf->ntype) : "(none)",
                  path->n_nodes);
    for (size_t i = 0; i < path->n_nodes; i++) {
        char buf[DCG_VAR_STRING_MAXLEN];
        (void) c_dcg_var_format(&path->eval_val[i], buf, sizeof(buf));
        (void) printf("      [%zu] %-11s %-14s %s\n", i, c_dcg_node_type_name(path->node[i]->ntype), path->node[i]->repr ? path->node[i]->repr : "", buf);
    }
}

/* The stage mask of a node that got all the way through, with the producer bit
 * of whatever produced its value. An OPERATOR node is evaluated by the rule its
 * type carries when the build injects rules, and by the built-in evaluation when
 * it dispatches - the same function either way, so the value is the same and only
 * the record differs. */
#if DCG_EVAL_DIRECT_HOOKS
#define T_RULE_PRODUCER DCG_EVAL_STAGE_TYPE_RULE
#else
#define T_RULE_PRODUCER DCG_EVAL_STAGE_BUILTIN
#endif

#define T_FULL_STAGE(producer) (DCG_EVAL_STAGE_PRE_EVAL | DCG_EVAL_STAGE_EVAL | DCG_EVAL_STAGE_POST_EVAL | DCG_EVAL_STAGE_DONE | (producer))

// ========== Hook fixtures ==========

/** What the hooks of a test node did, and where the test wanted them to fail. */
typedef struct t_hook_log {
    int       pre_calls;
    int       eval_calls;
    int       post_calls;
    int       fail_stage;  // 1 = pre, 2 = eval, 3 = post, 0 = never.
    dcg_node* last_node;   // The node the last hook was called on.
} t_hook_log;

static int t_hook_pre(dcg_node* node, void* user_data) {
    t_hook_log* log = (t_hook_log*) user_data;
    log->pre_calls++;
    log->last_node = node;
    return log->fail_stage == 1 ? DCG_ERR_BUSY : DCG_OK;
}

static int t_hook_eval(dcg_node* node, void* user_data) {
    t_hook_log* log = (t_hook_log*) user_data;
    log->eval_calls++;
    log->last_node = node;
    if (log->fail_stage == 2) return DCG_ERR_BUSY;
    return c_dcg_var_init_int(&node->out, 7); /* the hook's own value, not the node type's */
}

static int t_hook_post(dcg_node* node, void* user_data) {
    t_hook_log* log = (t_hook_log*) user_data;
    log->post_calls++;
    log->last_node = node;
    return log->fail_stage == 3 ? DCG_ERR_BUSY : DCG_OK;
}

static void t_hook_install(dcg_node* node, t_hook_log* log, bool pre, bool eval, bool post) {
    if (pre) (void) c_dcg_node_register_eval_hook(node, DCG_HOOK_PRE_EVAL, t_hook_pre, log);
    if (eval) (void) c_dcg_node_register_eval_hook(node, DCG_HOOK_EVAL, t_hook_eval, log);
    if (post) (void) c_dcg_node_register_eval_hook(node, DCG_HOOK_POST_EVAL, t_hook_post, log);
}

/** An observer that counts the EVALUATED announcements and keeps the last one. */
static int       t_event_count     = 0;
static uint64_t  t_event_last_seq  = 0;
static dcg_node* t_event_last_node = NULL;

static void t_on_event(dcg_node_event event, dcg_node* self, dcg_node* subject, uint64_t seq_id, void* user_data) {
    (void) subject;
    (void) user_data;
    if (event != DCG_NODE_EVENT_EVALUATED) return;
    t_event_count++;
    t_event_last_seq  = seq_id;
    t_event_last_node = self;
}

// ========== One node: the eval, and the dry run ==========

static void test_a_literal_evaluates_to_itself(void) {
    dcg_node* literal = t_num(2.5);
    DCG_CHECK_INT(c_dcg_node_eval(literal), DCG_OK);
    DCG_CHECK_INT(literal->out.dtype, VAR_TYPE_DOUBLE);
    DCG_CHECK(literal->out.value.as_double == 2.5);
    DCG_CHECK_INT(literal->eval_ctx.visits, 0); /* one node, not a walk: no visit */
    t_trace_eval("2.5", literal);
    t_free(literal);
}

static void test_one_node_keeps_the_value_it_produced(void) {
    dcg_node* node = t_binary(DCG_OP_ADD, 1.0, 2.0);
    (void) c_dcg_var_init_int(&node->out, 99);

    DCG_CHECK_INT(c_dcg_node_eval(node), DCG_OK);
    DCG_CHECK_INT(node->out.dtype, VAR_TYPE_DOUBLE);
    DCG_CHECK(node->out.value.as_double == 3.0); /* the slot the call was made on */
    t_trace_eval("add, in place", node);

    t_free(node);
}

static void test_the_dry_run_hands_the_value_out(void) {
    dcg_node* node = t_binary(DCG_OP_ADD, 1.0, 2.0);
    dcg_var_t value;
    (void) c_dcg_var_init(&value);
    (void) c_dcg_var_init_int(&node->out, 99);

    DCG_CHECK_INT(c_dcg_node_dryrun(node, &value), DCG_OK);
    DCG_CHECK_INT(value.dtype, VAR_TYPE_DOUBLE);
    DCG_CHECK(value.value.as_double == 3.0); /* the answer, and the caller holds it */
    DCG_CHECK_INT(node->out.dtype, VAR_TYPE_INT);
    DCG_CHECK_INT(node->out.value.as_int, 99); /* while the node keeps what it came in with */
    DCG_CHECK_INT(node->eval_ctx.err_code, DCG_OK);
    DCG_CHECK_INT(node->eval_ctx.stage, T_FULL_STAGE(T_RULE_PRODUCER));
    t_trace_eval("add, dry run", node);

    /* Asked again with nowhere to put the answer: the outcome is still reported
     * and the value is released rather than left behind. */
    DCG_CHECK_INT(c_dcg_node_dryrun(node, NULL), DCG_OK);
    DCG_CHECK_INT(node->out.dtype, VAR_TYPE_INT);
    DCG_CHECK_INT(node->out.value.as_int, 99);

    c_dcg_var_dealloc(&value);
    t_free(node);
}

static void test_one_node_leaves_the_run_state_alone(void) {
    /* Evaluating one node is not a walk, so nothing about a run is written on it:
     * it does not count a visit, does not claim the depth and does not stamp a run
     * id - the dry run no more than the eval. What BOTH leave is their own
     * outcome - how far they got and what they ended with - because those are
     * about this evaluation, and a caller told "it failed" is owed the stage it
     * failed at. */
    dcg_node* node = t_binary(DCG_OP_ADD, 1.0, 2.0);
    node->eval_ctx.visits      = 3;
    node->eval_ctx.depth       = 2;
    node->eval_ctx.eval_seq_id = 0xABCD;
    node->eval_ctx.stage       = DCG_EVAL_STAGE_NONE;
    node->eval_ctx.err_code    = DCG_ERR_BUSY;

    DCG_CHECK_INT(c_dcg_node_eval(node), DCG_OK);
    DCG_CHECK_INT(node->eval_ctx.visits, 3);
    DCG_CHECK_INT(node->eval_ctx.depth, 2);
    DCG_CHECK_INT(node->eval_ctx.eval_seq_id, 0xABCD);
    DCG_CHECK_INT(node->eval_ctx.err_code, DCG_OK);
    DCG_CHECK_INT(node->eval_ctx.stage, T_FULL_STAGE(T_RULE_PRODUCER));
    t_trace_eval("add, one node", node);

    /* One node that FAILED says where it stopped, on the node's own context - and
     * what the dry run hands out is the node's slot as it stands. */
    dcg_mapping_lgroup* store = dcg_t_mapping_lgroup(4, "dry");
    dcg_var_t*          slot  = NULL;
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_create_slot(store, "x", 1, NULL, &slot), DCG_OK);
    dcg_variable_node* read = c_dcg_mapping_lgroup_get_node(store, "x", 1, NULL);

    dcg_var_t value;
    (void) c_dcg_var_init(&value);
    DCG_CHECK_INT(c_dcg_node_dryrun(&read->base, &value), DCG_ERR_UNBOUND);
    DCG_CHECK_INT(value.dtype, VAR_TYPE_INFERRED);                     /* the entry by offset, unresolved */
    DCG_CHECK_INT(read->base.eval_ctx.err_code, DCG_ERR_UNBOUND);
    DCG_CHECK_INT(read->base.eval_ctx.stage, DCG_EVAL_STAGE_PRE_EVAL | DCG_EVAL_STAGE_TYPE_RULE); /* the stage it got through, and who was running */
    DCG_CHECK_INT(read->base.eval_ctx.eval_seq_id, 0);                 /* and still no run */
    t_trace_eval("read, dry run", &read->base);

    c_dcg_mapping_lgroup_free(store);
    t_free(node);
}

// ========== One node: the built-in rules ==========

static void test_a_variable_reads_the_slot_it_reflects(void) {
    dcg_mapping_lgroup* store = dcg_t_mapping_lgroup(4, "store");
    dcg_var_t*          slot  = NULL;

    /* An entry the build reserved and nothing has landed in: the read cannot be
     * answered at all, and the layer says which of the two things went wrong. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_create_slot(store, "x", 1, NULL, &slot), DCG_OK);
    DCG_CHECK(slot != NULL);
    DCG_CHECK_INT(slot->dtype, VAR_TYPE_RESERVED);

    dcg_variable_node* read = c_dcg_mapping_lgroup_get_node(store, "x", 1, NULL);
    DCG_CHECK(read != NULL);
    DCG_CHECK_INT(read->base.out.dtype, VAR_TYPE_INFERRED); /* where the entry is, and no type yet */
    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_ERR_UNBOUND);
    t_trace_eval("read (reserved)", &read->base);

    /* The value arrives and the very same node reads it: the slot is what the
     * node holds, not a value it copied. The tag was VAR_TYPE_INFERRED - the entry
     * by offset, and no type to read - and evaluating is what spends the offset
     * and leaves the entry itself in its place. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "x", 1, 3.5), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_OK);
    DCG_CHECK_INT(read->base.out.dtype, VAR_TYPE_DOUBLE_REF);
    DCG_CHECK(read->base.out.value.as_ref == (const void*) &slot->value);
    DCG_CHECK(c_dcg_var_as_double(&read->base.out) == 3.5);
    t_trace_eval("read (filled)", &read->base);

    /* A second evaluation resolves nothing again: the tag is a fact by then. */
    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_OK);
    DCG_CHECK_INT(read->base.out.dtype, VAR_TYPE_DOUBLE_REF);

    dcg_t_trace_mapping("store", store);
    c_dcg_mapping_lgroup_free(store); /* the group owns its reads */
}

static void test_a_read_built_before_a_growth_reads_where_the_entry_moved_to(void) {
    /* A store that takes more entries MOVES the block every entry lives in, so a
     * pointer taken when a read was built would be wrong the moment the store
     * grew. A read that has not been evaluated yet holds the entry's OFFSET
     * instead - an index into whatever block the store has now - so everything
     * that happens between the build and the first evaluation happens before the
     * read has looked anywhere. */
    dcg_mapping_lgroup* store = dcg_t_mapping_lgroup(2, "grow");
    dcg_var_t*          slot  = NULL;
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_create_slot(store, "x", 1, NULL, &slot), DCG_OK);

    dcg_variable_node* read = c_dcg_mapping_lgroup_get_node(store, "x", 1, NULL);
    DCG_CHECK(read != NULL);
    DCG_CHECK_INT(read->base.out.value.as_offset, 0); /* the entry, by offset, and nothing else */

    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "x", 1, 1.5), DCG_OK);

    /* Enough new entries to outgrow the block several times over - all of it
     * before the read has been evaluated once. */
    for (int i = 0; i < 24; i++) {
        char key[32];
        (void) snprintf(key, sizeof(key), "later%d", i);
        DCG_CHECK_INT(c_dcg_mapping_lgroup_set_int(store, key, strlen(key), i), DCG_OK);
    }

    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_OK);
    DCG_CHECK(c_dcg_var_as_double(&read->base.out) == 1.5); /* the entry, where it moved to */
    DCG_CHECK_INT(read->base.out.dtype, VAR_TYPE_DOUBLE_REF);
    t_trace_eval("read after 24 grows", &read->base);

    /* The resolution left a reference into that block, so the block has to stay
     * where it is from here on: a store is not grown under a graph that reads it
     * (the bake pass is what will hold it to that - a store carries `frozen` for
     * the caller who wants it held to now). What the resolution did NOT pin is the
     * value: a write of the type it resolved to is read through. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "x", 1, 8.25), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_OK);
    DCG_CHECK(c_dcg_var_as_double(&read->base.out) == 8.25);

    /* And a graph over that read decides on the value of the moment. */
    dcg_node* positive = t_binary_of_num(DCG_OP_GT, &read->base, 0.0);
    DCG_CHECK_INT(c_dcg_node_eval(positive), DCG_OK);
    DCG_CHECK(positive->out.value.as_bool);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "x", 1, -3.0), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_eval(positive), DCG_OK);
    DCG_CHECK(!positive->out.value.as_bool);
    t_free(positive);

    c_dcg_mapping_lgroup_free(store);
}

static void test_a_read_is_resolved_to_the_type_the_entry_holds(void) {
    /* A store entry's type is not fixed: a later write may land a value of
     * another type. What settles a read's type is its FIRST evaluation - the
     * entry's type as of then - and from that moment the read is an answer
     * rather than a question: a second evaluation leaves the slot exactly as the
     * resolution left it, and that is the fast path the rule exists for. */
    dcg_mapping_lgroup* store = dcg_t_mapping_lgroup(4, "changing");
    dcg_var_t*          slot  = NULL;
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_create_slot(store, "x", 1, NULL, &slot), DCG_OK);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_create_slot(store, "y", 1, NULL, &slot), DCG_OK);

    dcg_variable_node* read = c_dcg_mapping_lgroup_get_node(store, "x", 1, NULL);
    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_ERR_UNBOUND); /* born with the offset; the entry holds nothing */

    /* The type of the moment is the type the read is resolved to. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_int(store, "x", 1, 7), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_OK);
    DCG_CHECK_INT(read->base.out.dtype, VAR_TYPE_INT_REF);
    DCG_CHECK_INT(c_dcg_var_as_int(&read->base.out), 7);
    t_trace_eval("read(x), int", &read->base);

    /* The fast path: a second evaluation resolves nothing again - same tag, same
     * entry - and the value behind it is still live. */
    const void* resolved = read->base.out.value.as_ref;
    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_OK);
    DCG_CHECK_INT(read->base.out.dtype, VAR_TYPE_INT_REF);
    DCG_CHECK(read->base.out.value.as_ref == resolved);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_int(store, "x", 1, 9), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_as_int(&read->base.out), 9);

    /* A read built over an entry holding another type is born over THAT type: it
     * is the entry and the moment of the first evaluation that decide, and a read
     * still holding its offset has not been evaluated yet. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "y", 1, 3.5), DCG_OK);
    dcg_variable_node* later = c_dcg_mapping_lgroup_get_node(store, "y", 1, NULL);
    DCG_CHECK_INT(c_dcg_node_eval(&later->base), DCG_OK);
    DCG_CHECK_INT(later->base.out.dtype, VAR_TYPE_DOUBLE_REF);
    DCG_CHECK(c_dcg_var_as_double(&later->base.out) == 3.5);

    /* And a graph over the resolved read decides on it, read as a number: 9 > 0. */
    dcg_node* positive = t_binary_of_num(DCG_OP_GT, &read->base, 0.0);
    DCG_CHECK_INT(c_dcg_node_eval(positive), DCG_OK);
    DCG_CHECK(positive->out.value.as_bool);
    t_free(positive);

    c_dcg_mapping_lgroup_free(store);
}

static void test_a_retyped_entry_is_resolved_again(void) {
    /* The fast path holds a reference, and a reference PROMISES the type of the
     * entry it names. Whether that promise can be broken under a read is the
     * store's side of it (DCG_MAPPING_IMMUTABLE_DTYPE), and this is what each
     * side answers:
     *
     *   - with the entry's type immutable (the default), the RETYPE ITSELF is
     *     refused: the read keeps reading what the entry holds, and the promise
     *     is never broken;
     *   - with a store that may retype, the entry becomes another type under the
     *     read, which resolves again and answers with the entry as it is now
     *     rather than reading a double through a string's tag.
     */
    dcg_mapping_lgroup* store = dcg_t_mapping_lgroup(4, "retype");
    dcg_var_t           text  = dcg_t_var_string("text");
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set(store, "s", 1, &text), DCG_OK);

    dcg_variable_node* read = c_dcg_mapping_lgroup_get_node(store, "s", 1, NULL);
    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_OK);
    DCG_CHECK_INT(read->base.out.dtype, VAR_TYPE_STRING_REF);
    DCG_CHECK_STR(c_dcg_var_as_string(&read->base.out), "text");
    t_trace_eval("read(s), string", &read->base);

#if DCG_MAPPING_IMMUTABLE_DTYPE
    /* The write of another type is refused, and the read is untouched by it. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "s", 1, 2.5), DCG_ERR_TYPE);
    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_OK);
    DCG_CHECK_INT(read->base.out.dtype, VAR_TYPE_STRING_REF);
    DCG_CHECK_STR(c_dcg_var_as_string(&read->base.out), "text");

    /* What the entry takes is a string, and the read follows it. */
    dcg_var_t again = dcg_t_var_string("again");
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set(store, "s", 1, &again), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_OK);
    DCG_CHECK_STR(c_dcg_var_as_string(&read->base.out), "again");
    t_trace_eval("read(s), rewritten", &read->base);
#else
    /* The entry becomes a double under the read: the slot it refers to is no
     * longer a string, so the read resolves again and reads the number. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "s", 1, 2.5), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_OK);
    DCG_CHECK_INT(read->base.out.dtype, VAR_TYPE_DOUBLE_REF);
    DCG_CHECK(c_dcg_var_as_double(&read->base.out) == 2.5);
    t_trace_eval("read(s), double", &read->base);

    /* And back: the promise is whatever the entry holds at the evaluation. */
    dcg_var_t again = dcg_t_var_string("again");
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set(store, "s", 1, &again), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_OK);
    DCG_CHECK_INT(read->base.out.dtype, VAR_TYPE_STRING_REF);
    DCG_CHECK_STR(c_dcg_var_as_string(&read->base.out), "again");
#endif

    c_dcg_mapping_lgroup_free(store);
}

static void test_a_frozen_stores_read_is_resolved_by_its_first_evaluation(void) {
    /* A frozen store's read is a read like any other: born holding the entry's
     * offset, resolved by its first evaluation to what the entry holds then. The
     * freeze decides that the store takes no more ENTRIES - it cannot decide what
     * an entry holds, and it does not stop an entry being written. */
    dcg_mapping_lgroup* store = dcg_t_mapping_lgroup(4, "frozen");
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "x", 1, 2.5), DCG_OK);
    store->frozen = true;

    dcg_variable_node* read = c_dcg_mapping_lgroup_get_node(store, "x", 1, NULL);
    DCG_CHECK(read != NULL);
    DCG_CHECK_INT(read->base.out.dtype, VAR_TYPE_INFERRED); /* unresolved: the entry, by offset */

    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_OK);
    DCG_CHECK_INT(read->base.out.dtype, VAR_TYPE_DOUBLE_REF); /* the entry's answer, given now */
    DCG_CHECK(c_dcg_var_as_double(&read->base.out) == 2.5);

    /* And a live one: a frozen store's entries are still written. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "x", 1, 4.5), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_OK);
    DCG_CHECK(c_dcg_var_as_double(&read->base.out) == 4.5);
    t_trace_eval("frozen read(x)", &read->base);

    c_dcg_mapping_lgroup_free(store);
}

static void test_a_reserved_entry_is_answered_after_the_freeze(void) {
    /* The workflow this guards, in order, because the order is the whole of it:
     * a new store, an entry reserved in it and left holding nothing, THEN the
     * freeze, THEN a read built over that reserved entry - and the value arriving
     * afterwards.
     *
     * Nothing here is a special case: the read holds the entry's offset, and the
     * entry is what answers. A freeze decides that the store takes no more
     * entries; it cannot decide that an entry which was empty at the freeze stays
     * empty, because that is the entry's to say, and it says so at the evaluation
     * rather than at the freeze. */
    dcg_mapping_lgroup* store = dcg_t_mapping_lgroup(4, "frozen_late");
    dcg_var_t*          slot  = NULL;

    /* 1. Reserve, by hand and by name: the entry exists and holds nothing. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_create_slot(store, "open", 4, NULL, &slot), DCG_OK);
    DCG_CHECK_INT(slot->dtype, VAR_TYPE_RESERVED);

    /* 2. Freeze the store - no more entries - with that one still empty. */
    store->frozen = true;

    /* 3. A read built over the reserved entry, while the store is frozen. */
    dcg_variable_node* read = c_dcg_mapping_lgroup_get_node(store, "open", 4, NULL);
    DCG_CHECK(read != NULL);
    DCG_CHECK_INT(read->base.out.dtype, VAR_TYPE_INFERRED);
    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_ERR_UNBOUND); /* nothing there to answer with */
    t_trace_eval("frozen read(open), reserved", &read->base);

    /* 4. The value arrives - a frozen store takes no new ENTRIES, and this one is
     * not new - and the read that could not be answered answers. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "open", 4, 1.25), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_OK);
    DCG_CHECK_INT(read->base.out.dtype, VAR_TYPE_DOUBLE_REF);
    DCG_CHECK(c_dcg_var_as_double(&read->base.out) == 1.25);
    t_trace_eval("frozen read(open), filled", &read->base);

    /* 5. A second read over the same entry, made after the value landed, resolves
     * the same way - it is the entry that answers, not the moment of the build. */
    dcg_variable_node* second = c_dcg_mapping_lgroup_get_node(store, "open", 4, NULL);
    DCG_CHECK_INT(c_dcg_node_eval(&second->base), DCG_OK);
    DCG_CHECK(c_dcg_var_as_double(&second->base.out) == 1.25);

    /* 6. And the freeze still means what it says: no new entry appears. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_create_slot(store, "later", 5, NULL, &slot), DCG_ERR_BUSY);

    /* 7. A graph over the resolved read decides on the value of the moment. */
    dcg_node* positive = t_binary_of_num(DCG_OP_GT, &read->base, 0.0);
    DCG_CHECK_INT(c_dcg_node_eval(positive), DCG_OK);
    DCG_CHECK(positive->out.value.as_bool);
    t_free(positive);

    c_dcg_mapping_lgroup_free(store);
}

static void test_an_operand_of_an_operand_is_still_live(void) {
    /* An expression used as an operand of another one: the outer slot must keep
     * REFERRING to the inner node's value, not hold a copy of what it said the
     * first time it ran - a copy would freeze the decision at the first
     * evaluation and every later walk would repeat it. */
    dcg_mapping_lgroup* store = dcg_t_mapping_lgroup(4, "nested");
    dcg_var_t*          slot  = NULL;
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_create_slot(store, "close", 5, NULL, &slot), DCG_OK);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_create_slot(store, "open", 4, NULL, &slot), DCG_OK);

    dcg_variable_node*   close    = c_dcg_mapping_lgroup_get_node(store, "close", 5, NULL);
    dcg_variable_node*   open_    = c_dcg_mapping_lgroup_get_node(store, "open", 4, NULL);
    dcg_constant_node*   zero     = c_dcg_node_new_const_double(0.0, NULL);
    dcg_expression_node* spread   = c_dcg_node_new_expr_binary(DCG_OP_SUB, &close->base, &open_->base, NULL);
    dcg_expression_node* positive = c_dcg_node_new_expr_binary(DCG_OP_GT, &spread->base, &zero->base, NULL);
    c_ap_decref(&zero->base);

    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "close", 5, 10.5), DCG_OK);
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "open", 4, 8.0), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_eval(&positive->base), DCG_OK);
    DCG_CHECK(positive->base.out.value.as_bool);
    DCG_CHECK(c_dcg_var_as_double(&spread->base.out) == 2.5);
    /* The workspace holds what the operand READS AS: the dereference happened
     * when the walk filled the slot, so the operator is applied to a value. */
    DCG_CHECK_INT(positive->args[0].dtype, VAR_TYPE_DOUBLE);
    DCG_CHECK(c_dcg_var_as_double(&positive->args[0]) == 2.5);
    DCG_CHECK_INT(close->base.out.dtype, VAR_TYPE_DOUBLE_REF); /* a read keeps the live reference */

    /* The entry moves, and the outer expression decides on the new value - the
     * slot is re-made over the operand, so its tag is the operand's as of now. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "open", 4, 20.0), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_eval(&positive->base), DCG_OK);
    DCG_CHECK(!positive->base.out.value.as_bool);
    DCG_CHECK(c_dcg_var_as_double(&spread->base.out) == -9.5);
    DCG_CHECK(c_dcg_var_as_double(&positive->args[0]) == -9.5);
    t_trace_eval("spread > 0", &positive->base);

    c_dcg_node_free_generic(&positive->base);
    c_dcg_node_free_generic(&spread->base);
    c_dcg_mapping_lgroup_free(store);
}

static void test_a_variable_that_reflects_nothing_says_so(void) {
    dcg_variable_node* unbound = dcg_t_node_var("n", NULL, NULL);
    DCG_CHECK_INT(unbound->base.out.dtype, VAR_TYPE_RAW_PTR);
    DCG_CHECK_INT(c_dcg_node_eval(&unbound->base), DCG_ERR_UNBOUND);

    /* A variable whose slot holds a value of its own is not a read at all: the
     * node is malformed and the layer names that, rather than reading a number
     * out of a node with no entry behind it. */
    (void) c_dcg_var_init_int(&unbound->base.out, 5);
    DCG_CHECK_INT(c_dcg_node_eval(&unbound->base), DCG_ERR_TYPE);
    c_dcg_node_free_var(unbound);
}

static void test_the_operators_evaluate_by_their_operands(void) {
    /* Every row names the operand types it is built over, because the type of an
     * arithmetic result follows them: a whole-number pair stays whole, and one
     * double in the pair makes the result a double. */
    struct {
        dcg_op_code  op;
        bool         whole; /* operands are ints when true, doubles when false */
        double       x;
        double       y;
        dcg_var_type dtype;
        double       expected;
    } cases[] = {
        {DCG_OP_ADD, true, 1, 2, VAR_TYPE_INT, 3},
        {DCG_OP_SUB, true, 1, 2, VAR_TYPE_INT, -1},
        {DCG_OP_MUL, true, 3, 4, VAR_TYPE_INT, 12},
        {DCG_OP_DIV, true, 7, 2, VAR_TYPE_DOUBLE, 3.5},    /* Python's `/` is a float division */
        {DCG_OP_FLOORDIV, true, 7, 2, VAR_TYPE_INT, 3},
        {DCG_OP_FLOORDIV, true, -7, 2, VAR_TYPE_INT, -4},  /* floored, not truncated */
        {DCG_OP_FLOORDIV, false, 7.0, 2, VAR_TYPE_DOUBLE, 3.0},
        {DCG_OP_POW, true, 2, 10, VAR_TYPE_INT, 1024},
        {DCG_OP_POW, true, 2, -1, VAR_TYPE_DOUBLE, 0.5},
        {DCG_OP_POW, false, 2.0, 2, VAR_TYPE_DOUBLE, 4.0},
        {DCG_OP_POW, false, 4, 0.5, VAR_TYPE_DOUBLE, 2.0}, /* a fractional exponent is a real power */
        {DCG_OP_ADD, false, 1.5, 2, VAR_TYPE_DOUBLE, 3.5}, /* a double operand makes a double result */
        {DCG_OP_EQ, true, 2, 2, VAR_TYPE_BOOL, 1},
        {DCG_OP_EQ, true, 2, 3, VAR_TYPE_BOOL, 0},
        {DCG_OP_EQ, false, 2, 2, VAR_TYPE_BOOL, 1},
        {DCG_OP_NE, true, 2, 3, VAR_TYPE_BOOL, 1},
        {DCG_OP_GT, true, 3, 2, VAR_TYPE_BOOL, 1},
        {DCG_OP_GE, true, 2, 2, VAR_TYPE_BOOL, 1},
        {DCG_OP_LT, true, 2, 2, VAR_TYPE_BOOL, 0},
        {DCG_OP_LE, true, 2, 3, VAR_TYPE_BOOL, 1},
        {DCG_OP_AND, true, 1, 5, VAR_TYPE_INT, 5},         /* the left operand decides, as in Python */
        {DCG_OP_AND, true, 0, 5, VAR_TYPE_INT, 0},
        {DCG_OP_OR, true, 1, 5, VAR_TYPE_INT, 1},
        {DCG_OP_OR, true, 0, 5, VAR_TYPE_INT, 5},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        dcg_node* node     = cases[i].whole ? t_binary_int(cases[i].op, (ssize_t) cases[i].x, (ssize_t) cases[i].y) : t_binary(cases[i].op, cases[i].x, cases[i].y);
        int       ret_code = c_dcg_node_eval(node);

        if (ret_code != DCG_OK) {
            (void) fprintf(stderr, "  FAIL %s over %g and %g: %s\n", c_dcg_op_code_name(cases[i].op), cases[i].x, cases[i].y, c_dcg_ret_code_name(ret_code));
            dcg_test_failures++;
            dcg_test_checks++;
            t_free(node);
            continue;
        }

        DCG_CHECK_INT(node->out.dtype, cases[i].dtype);

        double actual;
        switch (node->out.dtype) {
            case VAR_TYPE_INT:
                actual = (double) node->out.value.as_int;
                break;
            case VAR_TYPE_BOOL:
                actual = node->out.value.as_bool ? 1.0 : 0.0;
                break;
            default:
                actual = c_dcg_var_as_double(&node->out);
                break;
        }

        if (cases[i].expected != actual) {
            (void) fprintf(stderr, "  FAIL %s over %g and %g: %g != %g\n", c_dcg_op_code_name(cases[i].op), cases[i].x, cases[i].y, actual, cases[i].expected);
            dcg_test_failures++;
        }
        dcg_test_checks++;
        t_free(node);
    }

    /* One whole-number operand and one double: the double decides the result's
     * type, whichever side it is on. */
    dcg_node* whole = dcg_t_node_int(NULL, 4);
    dcg_node* mixed = t_binary_of_num(DCG_OP_POW, whole, 0.5);
    DCG_CHECK_INT(c_dcg_node_eval(mixed), DCG_OK);
    DCG_CHECK_INT(mixed->out.dtype, VAR_TYPE_DOUBLE);
    DCG_CHECK(mixed->out.value.as_double == 2.0);
    t_free(mixed);
    c_ap_decref(whole); /* bound into the expression: this reference goes back */
}

static void test_the_unary_operators(void) {
    dcg_node* negated = t_unary(DCG_OP_NEG, 2.5);
    DCG_CHECK_INT(c_dcg_node_eval(negated), DCG_OK);
    DCG_CHECK(c_dcg_var_as_double(&negated->out) == -2.5);

    dcg_node* inverted = t_unary(DCG_OP_NOT, 0.0);
    DCG_CHECK_INT(c_dcg_node_eval(inverted), DCG_OK);
    DCG_CHECK_INT(inverted->out.dtype, VAR_TYPE_BOOL);
    DCG_CHECK(inverted->out.value.as_bool);

    t_free(negated);
    t_free(inverted);
}

static void test_an_operand_outside_the_domain_is_refused(void) {
    dcg_node* divided = t_binary(DCG_OP_DIV, 1.0, 0.0);
    DCG_CHECK_INT(c_dcg_node_eval(divided), DCG_ERR_MATH);
    t_free(divided);

    dcg_node* floored = t_binary(DCG_OP_FLOORDIV, 2, 0);
    DCG_CHECK_INT(c_dcg_node_eval(floored), DCG_ERR_MATH);
    t_free(floored);

    /* A string has no arithmetic: what refuses is the tag of the operand, not
     * the operator. */
    dcg_node* text    = dcg_t_node_string(NULL, "text");
    dcg_node* sum     = t_binary_of_num(DCG_OP_ADD, text, 1.0);
    DCG_CHECK_INT(c_dcg_node_eval(sum), DCG_ERR_MATH);

    dcg_node* negated = t_unary_string(DCG_OP_NEG, "also text");
    DCG_CHECK_INT(c_dcg_node_eval(negated), DCG_ERR_MATH);

    /* A comparison with no order in it is refused too, where equality is not:
     * `"a" < 1` has no answer, and a string sorts against a string. */
    dcg_node* ordered = t_binary_of_num(DCG_OP_LT, text, 1.0);
    DCG_CHECK_INT(c_dcg_node_eval(ordered), DCG_ERR_MATH);

    t_free(sum);
    t_free(ordered);
    t_free(text);
    t_free(negated);
}

static void test_an_expression_over_a_read_is_evaluated_live(void) {
    dcg_mapping_lgroup* store = dcg_t_mapping_lgroup(4, "live");
    dcg_var_t*          slot  = NULL;
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_create_slot(store, "signal", 6, NULL, &slot), DCG_OK);

    dcg_variable_node* read     = c_dcg_mapping_lgroup_get_node(store, "signal", 6, NULL);
    dcg_node*          positive = t_binary_of_num(DCG_OP_GT, &read->base, 0.0);

    /* The store has not been filled: the operand is a reference to a reserved
     * slot, so the expression cannot produce a value and says which. */
    DCG_CHECK_INT(c_dcg_node_eval(positive), DCG_ERR_UNBOUND);

    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "signal", 6, -1.5), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_eval(positive), DCG_OK);
    DCG_CHECK(!positive->out.value.as_bool);

    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "signal", 6, 2.0), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_eval(positive), DCG_OK);
    DCG_CHECK(positive->out.value.as_bool); /* the same node over the same slot, re-read */
    t_trace_eval("signal > 0", positive);

    t_free(positive); /* releases its hold on the read */
    c_dcg_mapping_lgroup_free(store);
}

static void test_a_ternary_reads_only_the_arm_it_takes(void) {
    dcg_node* condition = t_binary(DCG_OP_GT, 2.0, 1.0);
    dcg_node* yes       = t_num(10.0);
    dcg_node* no        = t_num(20.0);
    dcg_node* node      = t_ternary(condition, yes, no);

    DCG_CHECK_INT(c_dcg_node_eval(node), DCG_OK);
    DCG_CHECK(c_dcg_var_as_double(&node->out) == 10.0);
    t_trace_eval("2 > 1 ? 10 : 20", node);

    t_free(node);
    t_free(condition);
    t_free(yes);
    t_free(no);
}

static void test_a_call_has_no_evaluation(void) {
    dcg_node* call = dcg_t_node_call("spread");
    DCG_CHECK_INT(c_dcg_node_eval(call), DCG_ERR_TYPE);
    t_free(call);
}

static void test_an_action_evaluates_to_itself(void) {
    dcg_node* action = dcg_t_node_action(DCG_NODE_LONGACTION, "long");
    DCG_CHECK_INT(c_dcg_node_eval(action), DCG_OK);
    DCG_CHECK_INT(action->out.dtype, VAR_TYPE_NODE); /* the node itself, in a node-shaped slot */
    DCG_CHECK(c_dcg_var_as_node(&action->out) == (void*) action);
    t_trace_eval("long", action);
    t_free(action);
}

// ========== The walk ==========

/**
 * A four-level graph, built once per test that walks it:
 *
 *   root [true]
 *    +-- "signal > 0"        [no condition]
 *         +-- "signal > 1"   [true]
 *         |    +-- long      [true]
 *         |    +-- cancel    [false]
 *         +-- flat           [false]
 *
 * The root's own value is true, so its single edge is the one it takes; the arms
 * below are selected by the values the branches evaluate to.
 */
static dcg_node* t_deep_tree(dcg_mapping_lgroup* store, dcg_node** out_long) {
    dcg_var_t* slot = NULL;
    if (c_dcg_mapping_lgroup_get_create_slot(store, "signal", 6, NULL, &slot) != DCG_OK) return NULL;

    dcg_root_node*     root   = c_dcg_node_new_root(NULL, NULL);
    dcg_variable_node* signal = c_dcg_mapping_lgroup_get_node(store, "signal", 6, NULL);

    dcg_node* outer = t_binary_of_num(DCG_OP_GT, &signal->base, 0.0);
    dcg_node* inner = t_binary_of_num(DCG_OP_GT, &signal->base, 1.0);
    dcg_node* long_ = dcg_t_node_action(DCG_NODE_LONGACTION, "long");
    dcg_node* canc  = dcg_t_node_action(DCG_NODE_CANCELACTION, "cancel");
    dcg_node* flat  = dcg_t_node_action(DCG_NODE_CLEARACTION, "flat");

    t_attach(inner, long_, DCG_TRUE_CONDITION);
    t_attach(inner, canc, DCG_FALSE_CONDITION);
    t_attach(outer, inner, DCG_TRUE_CONDITION);
    t_attach(outer, flat, DCG_FALSE_CONDITION);
    t_attach(&root->base, outer, DCG_NO_CONDITION);

    *out_long = long_;
    return &root->base;
}

static void test_the_walk_lands_on_the_leaf_the_values_select(void) {
    dcg_mapping_lgroup* store = dcg_t_mapping_lgroup(4, "walk");
    dcg_node*           long_ = NULL;
    dcg_node*           root  = t_deep_tree(store, &long_);

    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "signal", 6, 2.0), DCG_OK);
    DCG_CHECK_INT(c_dcg_root_node_eval((dcg_root_node*) root), DCG_OK);

    const dcg_node_eval_path* path = &((dcg_root_node*) root)->eval_path;
    t_trace_path("signal = 2.0", path);

    DCG_CHECK_INT(path->code, DCG_OK);
    DCG_CHECK(path->leaf == long_);
    DCG_CHECK(path->failed == NULL);
    DCG_CHECK(path->seq_id != 0);
    DCG_CHECK_INT(path->n_nodes, 4); /* the root, the two branches and the leaf */

    /* The path is the walk, in order - not just where it ended. */
    DCG_CHECK_INT(path->node[0]->ntype, DCG_NODE_ROOT);
    DCG_CHECK(path->node[1] == root->children);
    DCG_CHECK_INT(path->eval_val[0].dtype, VAR_TYPE_BOOL); /* the root's own true */
    DCG_CHECK(path->eval_val[1].value.as_bool);
    DCG_CHECK(path->eval_val[2].value.as_bool);
    DCG_CHECK_INT(path->eval_val[3].dtype, VAR_TYPE_NODE); /* an action stands for itself */

    /* The other side of the same graph: a signal between the two thresholds
     * takes the inner fallback, and nothing below it is touched. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "signal", 6, 0.5), DCG_OK);
    DCG_CHECK_INT(c_dcg_root_node_eval((dcg_root_node*) root), DCG_OK);
    t_trace_path("signal = 0.5", path);
    DCG_CHECK_INT(path->n_nodes, 4);
    DCG_CHECK_INT(path->leaf->ntype, DCG_NODE_CANCELACTION);

    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "signal", 6, -1.0), DCG_OK);
    DCG_CHECK_INT(c_dcg_root_node_eval((dcg_root_node*) root), DCG_OK);
    t_trace_path("signal = -1.0", path);
    DCG_CHECK_INT(path->n_nodes, 3); /* the root, the outer branch and the leaf */
    DCG_CHECK_INT(path->leaf->ntype, DCG_NODE_CLEARACTION);

    dcg_t_trace_tree("the graph", root);
    c_dcg_node_teardown_root(root);
    c_dcg_mapping_lgroup_free(store);
}

static void test_the_true_arm_is_taken_first(void) {
    /* The first branch in the child list is the true arm, and it is the one a
     * value of true selects: the order the arms are stored is the order they are
     * checked. */
    dcg_root_node* root = c_dcg_node_new_root(NULL, NULL);
    dcg_node*      node = t_binary(DCG_OP_GT, 2.0, 1.0);
    dcg_node*      yes  = dcg_t_node_action(DCG_NODE_LONGACTION, "yes");
    dcg_node*      no   = dcg_t_node_action(DCG_NODE_SHORTACTION, "no");

    t_attach(node, yes, DCG_TRUE_CONDITION);
    t_attach(node, no, DCG_FALSE_CONDITION);
    t_attach(&root->base, node, DCG_NO_CONDITION);

    DCG_CHECK(node->children == yes); /* stored true first */
    DCG_CHECK_INT(c_dcg_root_node_eval(root), DCG_OK);
    DCG_CHECK(root->eval_path.leaf == yes);
    DCG_CHECK_INT(yes->eval_ctx.visits, 1);
    DCG_CHECK_INT(no->eval_ctx.visits, 0);

    c_dcg_node_teardown_root(&root->base);
}

static void test_the_else_arm_is_the_fallback(void) {
    /* The fallback is the LAST arm by the layer's own rule - nothing may be
     * appended behind it - and the walk takes it only after every other arm has
     * declined. The two are the same rule stated from either end, and the walk
     * holds it independently: it is given the list, not the build history. */
    dcg_root_node* root     = c_dcg_node_new_root(NULL, NULL);
    dcg_node*      node     = t_binary(DCG_OP_GT, 2.0, 1.0); /* evaluates true */
    dcg_node*      taken    = dcg_t_node_action(DCG_NODE_LONGACTION, "taken");
    dcg_node*      fallback = dcg_t_node_action(DCG_NODE_NOACTION, "fallback");

    t_attach(node, taken, DCG_TRUE_CONDITION);
    t_attach(node, fallback, DCG_ELSE_CONDITION);
    t_attach(&root->base, node, DCG_NO_CONDITION);
    DCG_CHECK(node->children == taken);
    DCG_CHECK(node->children->next_sibling == fallback); /* the fallback is last */

    DCG_CHECK_INT(c_dcg_root_node_eval(root), DCG_OK);
    DCG_CHECK(root->eval_path.leaf == taken);
    DCG_CHECK_INT(fallback->eval_ctx.visits, 0);

    /* A branch that evaluates false reaches no other arm, and the fallback is
     * what is left. */
    dcg_node* declined = t_binary(DCG_OP_GT, 1.0, 2.0);
    dcg_node* missed   = dcg_t_node_action(DCG_NODE_SHORTACTION, "missed");
    dcg_node* other    = dcg_t_node_action(DCG_NODE_NOACTION, "other");
    t_attach(declined, missed, DCG_TRUE_CONDITION);
    t_attach(declined, other, DCG_ELSE_CONDITION);
    ((dcg_root_node*) root)->eval_path.n_nodes = 0;
    /* A root is unary: it takes one entry child, and a second is refused by type
     * rather than by edge. */
    DCG_CHECK_INT(c_dcg_node_append(&root->base, declined, DCG_NO_CONDITION), DCG_ERR_TYPE);

    dcg_root_node* second = c_dcg_node_new_root(NULL, NULL);
    t_attach(&second->base, declined, DCG_NO_CONDITION);
    DCG_CHECK_INT(c_dcg_root_node_eval(second), DCG_OK);
    DCG_CHECK(second->eval_path.leaf == other);
    DCG_CHECK_INT(missed->eval_ctx.visits, 0);
    t_trace_path("the fallback", &second->eval_path);

    /* And nothing may be appended behind a fallback, which is what keeps it last
     * however the arms are written. The condition has to be one the node does not
     * already carry, or the duplicate check answers first. */
    dcg_node*                late = dcg_t_node_action(DCG_NODE_NOACTION, "late");
    dcg_node_edge_condition* fresh_edge = c_dcg_edge_new(dcg_t_var_int(99), "99", NULL);
    DCG_CHECK_INT(c_dcg_node_append(declined, late, fresh_edge), DCG_ERR_EDGE);
    c_dcg_condition_free(fresh_edge); /* refused, so nothing adopted it */
    t_free(late);

    c_dcg_node_teardown_root(&root->base);
    c_dcg_node_teardown_root(&second->base);
}

static void test_a_branch_that_matches_nothing_reports_no_match(void) {
    dcg_root_node* root = c_dcg_node_new_root(NULL, NULL);
    dcg_node*      node = t_binary(DCG_OP_GT, 2.0, 1.0);
    dcg_node*      kept = dcg_t_node_action(DCG_NODE_LONGACTION, "kept");

    /* The node evaluates to true, and its only arm is written for a value it
     * does not have: there is no edge to follow. The condition is a block, not a
     * local: a linked condition is adopted by the child and dies with it. */
    dcg_node_edge_condition* edge = c_dcg_edge_new(dcg_t_var_string("nope"), "nope", NULL);
    DCG_CHECK(edge != NULL);
    t_attach(node, kept, edge);
    t_attach(&root->base, node, DCG_NO_CONDITION);

    DCG_CHECK_INT(c_dcg_root_node_eval(root), DCG_ERR_NO_MATCH);
    DCG_CHECK(root->eval_path.leaf == NULL); /* nowhere landed */
    DCG_CHECK(root->eval_path.failed == node);
    DCG_CHECK_INT(root->eval_path.n_nodes, 2);
    t_trace_path("no match", &root->eval_path);

    c_dcg_node_teardown_root(&root->base);
}

static void test_a_breakpoint_passes_through_and_a_dangling_one_ends_the_walk(void) {
    dcg_root_node*       root = c_dcg_node_new_root(NULL, NULL);
    dcg_breakpoint_node* sink = c_dcg_node_new_breakpoint(NULL, "inspection", NULL);
    dcg_node*            leaf = dcg_t_node_action(DCG_NODE_LONGACTION, "after");

    DCG_CHECK(sink->base.eval_ctx.flags & DCG_EVAL_FLAG_BREAKPOINT);
    t_attach(&sink->base, leaf, DCG_NO_CONDITION);
    t_attach(&root->base, &sink->base, DCG_NO_CONDITION);

    DCG_CHECK_INT(c_dcg_root_node_eval(root), DCG_OK);
    DCG_CHECK(root->eval_path.leaf == leaf);
    DCG_CHECK_INT(root->eval_path.n_nodes, 3); /* the sink is on the record */
    DCG_CHECK(root->eval_path.node[1] == &sink->base);
    c_dcg_node_teardown_root(&root->base);

    /* A breakpoint nothing was connected to has nothing below it: the walk ends
     * there, and the record says where. */
    dcg_root_node*       lone_root = c_dcg_node_new_root(NULL, NULL);
    dcg_breakpoint_node* lone      = c_dcg_node_new_breakpoint(NULL, "dangling", NULL);
    t_attach(&lone_root->base, &lone->base, DCG_NO_CONDITION);

    DCG_CHECK_INT(c_dcg_root_node_eval(lone_root), DCG_OK);
    DCG_CHECK(lone_root->eval_path.leaf == &lone->base);
    DCG_CHECK_INT(lone_root->eval_path.n_nodes, 2);
    c_dcg_node_teardown_root(&lone_root->base);
}

static void test_the_walk_writes_the_bookkeeping_of_every_node_it_visits(void) {
    dcg_mapping_lgroup* store = dcg_t_mapping_lgroup(4, "ctx");
    dcg_node*           long_ = NULL;
    dcg_node*           root  = t_deep_tree(store, &long_);

    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "signal", 6, 2.0), DCG_OK);
    DCG_CHECK_INT(c_dcg_root_node_eval((dcg_root_node*) root), DCG_OK);

    uint64_t  seq_id    = ((dcg_root_node*) root)->eval_path.seq_id;
    dcg_node* nodes[4]  = {root, root->children, root->children->children, long_};
    size_t    depths[4] = {0, 1, 2, 3};
    /* Who produced each value on the way down: the root and the action leaf are
     * special and action nodes, which no rule is installed for - the built-in
     * evaluation answers them - and the two branches are operator nodes. */
    uint32_t full[4] = {
        T_FULL_STAGE(DCG_EVAL_STAGE_BUILTIN), /* the root */
        T_FULL_STAGE(T_RULE_PRODUCER),        /* the outer branch */
        T_FULL_STAGE(T_RULE_PRODUCER),        /* the inner branch */
        T_FULL_STAGE(DCG_EVAL_STAGE_BUILTIN), /* the action leaf */
    };

    for (size_t i = 0; i < 4; i++) {
        dcg_node* node = nodes[i];
        DCG_CHECK_INT(node->eval_ctx.err_code, DCG_OK);
        DCG_CHECK_INT(node->eval_ctx.stage, full[i]);
        DCG_CHECK_INT(node->eval_ctx.depth, depths[i]);
        DCG_CHECK_INT(node->eval_ctx.visits, 1);
        DCG_CHECK(node->eval_ctx.eval_seq_id == seq_id);
        DCG_CHECK(node->eval_ctx.run == NULL); /* lent for the visit, given back after */
        t_trace_eval(node->repr, node);
    }

    /* Every node off the path keeps the state it had, which is the point of the
     * walk stopping where it does: a second walk over other values visits again
     * and stamps a new run. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "signal", 6, -1.0), DCG_OK);
    DCG_CHECK_INT(c_dcg_root_node_eval((dcg_root_node*) root), DCG_OK);
    DCG_CHECK_INT(root->eval_ctx.visits, 2);
    DCG_CHECK_INT(long_->eval_ctx.visits, 1); /* not reached this time */
    DCG_CHECK(long_->eval_ctx.eval_seq_id == seq_id);
    DCG_CHECK(((dcg_root_node*) root)->eval_path.seq_id != seq_id);

    c_dcg_node_teardown_root(root);
    c_dcg_mapping_lgroup_free(store);
}

static void test_a_cached_node_keeps_the_value_it_holds(void) {
    dcg_node*   node = t_binary(DCG_OP_ADD, 1.0, 2.0);
    t_hook_log  log  = {0};
    t_hook_install(node, &log, false, true, false);

    (void) c_dcg_var_init_int(&node->out, 42);
    node->eval_ctx.flags |= DCG_EVAL_FLAG_CACHED;
    DCG_CHECK_INT(c_dcg_node_eval(node), DCG_OK);
    DCG_CHECK_INT(node->out.value.as_int, 42); /* the hook was not called */
    DCG_CHECK_INT(log.eval_calls, 0);

    node->eval_ctx.flags &= ~(uint64_t) DCG_EVAL_FLAG_CACHED;
    DCG_CHECK_INT(c_dcg_node_eval(node), DCG_OK);
    DCG_CHECK_INT(node->out.value.as_int, 7); /* and now it was */
    DCG_CHECK_INT(log.eval_calls, 1);
    t_free(node);
}

static void test_skip_children_ends_the_walk_where_it_stands(void) {
    dcg_root_node* root = c_dcg_node_new_root(NULL, NULL);
    dcg_node*      node = t_binary(DCG_OP_GT, 2.0, 1.0);
    dcg_node*      leaf = dcg_t_node_action(DCG_NODE_LONGACTION, "below");

    t_attach(node, leaf, DCG_TRUE_CONDITION);
    t_attach(&root->base, node, DCG_NO_CONDITION);

    node->eval_ctx.flags |= DCG_EVAL_FLAG_SKIP_CHILDREN;
    DCG_CHECK_INT(c_dcg_root_node_eval(root), DCG_OK);
    DCG_CHECK(root->eval_path.leaf == node); /* the branch itself, not what is under it */
    DCG_CHECK_INT(leaf->eval_ctx.visits, 0);
    c_dcg_node_teardown_root(&root->base);
}

static void test_a_hook_is_installed_once_and_its_data_has_to_agree(void) {
    dcg_node*  node  = t_num(1.0);
    t_hook_log log   = {0};
    t_hook_log other = {0};

    /* The three hooks of a node share ONE user_data, so the first registration
     * is what sets it - and a later one that disagrees is refused rather than
     * quietly splitting the node's three hooks across two owners. */
    DCG_CHECK_INT(c_dcg_node_register_eval_hook(node, DCG_HOOK_PRE_EVAL, t_hook_pre, &log), DCG_OK);
    DCG_CHECK(node->eval_ctx.user_data == &log);
    DCG_CHECK_INT(c_dcg_node_register_eval_hook(node, DCG_HOOK_EVAL, t_hook_eval, &log), DCG_OK);
    DCG_CHECK(node->eval_ctx.user_data == &log);

    DCG_CHECK_INT(c_dcg_node_register_eval_hook(node, DCG_HOOK_POST_EVAL, t_hook_post, &other), DCG_ERR_INVALID_ARG);
    DCG_CHECK(node->eval_ctx.post_eval_fn == NULL);

    /* One hook, one install: a slot that is taken is not replaced, so a hook is
     * never lost without its owner saying so. */
    DCG_CHECK_INT(c_dcg_node_register_eval_hook(node, DCG_HOOK_PRE_EVAL, t_hook_pre, &log), DCG_ERR_BUSY);
    DCG_CHECK_INT(c_dcg_node_register_eval_hook(node, DCG_HOOK_EVAL, t_hook_post, &log), DCG_ERR_BUSY);
    DCG_CHECK(node->eval_ctx.pre_eval_fn == t_hook_pre);

    /* A hook type that is not one of the three, and a hook that is not there. */
    DCG_CHECK_INT(c_dcg_node_register_eval_hook(node, (dcg_node_hook_type) 3, t_hook_pre, &log), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_node_register_eval_hook(node, DCG_HOOK_EVAL, NULL, &log), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_node_register_eval_hook(NULL, DCG_HOOK_EVAL, t_hook_eval, &log), DCG_ERR_INVALID_ARG);

    /* Giving the set back is what makes changing it possible - and the data goes
     * with it, so the next owner's is the one the hooks carry. */
    c_dcg_node_unregister_eval_hooks(node);
    DCG_CHECK(node->eval_ctx.pre_eval_fn == NULL);
    DCG_CHECK(node->eval_ctx.eval_fn == NULL);
    DCG_CHECK(node->eval_ctx.post_eval_fn == NULL);
    DCG_CHECK(node->eval_ctx.user_data == NULL);
    DCG_CHECK_INT(c_dcg_node_register_eval_hook(node, DCG_HOOK_POST_EVAL, t_hook_post, &other), DCG_OK);
    DCG_CHECK(node->eval_ctx.user_data == &other);

    /* And the node evaluates by its type again while nothing is installed. */
    c_dcg_node_unregister_eval_hooks(node);
    DCG_CHECK_INT(c_dcg_node_eval(node), DCG_OK);
    DCG_CHECK(node->out.value.as_double == 1.0);

    c_dcg_node_free(node);
}

static void test_a_record_snapshots_the_value_a_node_reads_as(void) {
    /* A slot holding a VALUE is copied as it stands; one holding a reference is
     * read through, so what a record keeps is the value and not the way to it. */
    dcg_constant_node* literal = c_dcg_node_new_const_double(2.5, NULL);
    dcg_var_t          snapshot;
    (void) c_dcg_var_init(&snapshot);

    c_dcg_var_snapshot(&snapshot, &literal->base.out);
    DCG_CHECK_INT(snapshot.dtype, VAR_TYPE_DOUBLE);
    DCG_CHECK(snapshot.value.as_double == 2.5);
    c_ap_decref(&literal->base);

    dcg_mapping_lgroup* store = dcg_t_mapping_lgroup(4, "snap");
    dcg_var_t*          slot  = NULL;
    DCG_CHECK_INT(c_dcg_mapping_lgroup_get_create_slot(store, "x", 1, NULL, &slot), DCG_OK);
    dcg_variable_node* read = c_dcg_mapping_lgroup_get_node(store, "x", 1, NULL);

    /* Nothing has landed in the entry and the read has not been evaluated: its
     * slot holds the entry's offset, tagged as a slot that has yet to be resolved, and the
     * a copy of that. */
    c_dcg_var_snapshot(&snapshot, &read->base.out);
    DCG_CHECK_INT(snapshot.dtype, VAR_TYPE_INFERRED);

    /* Once the value has landed, the node's evaluation is what catches the
     * reference's tag up with the entry - and the snapshot, taken after it the
     * way a record is, then holds the value itself. */
    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "x", 1, 4.5), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_eval(&read->base), DCG_OK);
    c_dcg_var_snapshot(&snapshot, &read->base.out);
    DCG_CHECK_INT(snapshot.dtype, VAR_TYPE_DOUBLE); /* the value, not the reference */
    DCG_CHECK(snapshot.value.as_double == 4.5);

    /* A slot that is not there at all leaves the snapshot empty. */
    c_dcg_var_snapshot(&snapshot, NULL);
    DCG_CHECK_INT(snapshot.dtype, VAR_TYPE_RAW_PTR);
    DCG_CHECK(snapshot.value.as_ptr == NULL);

    c_dcg_mapping_lgroup_free(store);
}

static void test_the_hooks_run_around_the_value_in_order(void) {
    dcg_node*  node = t_num(1.0);
    t_hook_log log  = {0};
    t_hook_install(node, &log, true, true, true);

    DCG_CHECK_INT(c_dcg_node_eval(node), DCG_OK);
    DCG_CHECK_INT(log.pre_calls, 1);
    DCG_CHECK_INT(log.eval_calls, 1);
    DCG_CHECK_INT(log.post_calls, 1);
    DCG_CHECK(log.last_node == node);
    DCG_CHECK_INT(node->out.value.as_int, 7); /* the hook produced the value, not the type */
    t_free(node);
}

static void test_a_failing_hook_stops_the_node_and_names_the_stage(void) {
    struct {
        int      fail_stage;
        int      expect_pre;
        int      expect_eval;
        int      expect_post;
        uint32_t expect_stage;
    } cases[] = {
        /* What the node got through, and what produced the value where one was
         * produced at all: the hook is the node's eval hook, so the producer bit
         * is HOOK as soon as the eval stage is reached. */
        {1, 1, 0, 0, DCG_EVAL_STAGE_NONE},
        {2, 1, 1, 0, DCG_EVAL_STAGE_PRE_EVAL | DCG_EVAL_STAGE_HOOK},
        {3, 1, 1, 1, DCG_EVAL_STAGE_PRE_EVAL | DCG_EVAL_STAGE_HOOK | DCG_EVAL_STAGE_EVAL},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        dcg_node*  node = t_num(1.0);
        t_hook_log log  = {0};
        log.fail_stage  = cases[i].fail_stage;
        t_hook_install(node, &log, true, true, true);

        DCG_CHECK_INT(c_dcg_node_eval_hooks(node), DCG_ERR_BUSY);
        DCG_CHECK_INT(log.pre_calls, cases[i].expect_pre);
        DCG_CHECK_INT(log.eval_calls, cases[i].expect_eval);
        DCG_CHECK_INT(log.post_calls, cases[i].expect_post);
        /* The protocol writes the record onto the node itself: what it got
         * through, and - in the same word - what produced the value. */
        DCG_CHECK_INT(node->eval_ctx.stage, cases[i].expect_stage);

        /* A node that stopped before its value was produced still holds what it
         * came in with; one that failed in the POST hook holds the value its eval
         * hook produced, because that value was there when the hook refused. */
        DCG_CHECK_INT(c_dcg_node_eval(node), DCG_ERR_BUSY);
        DCG_CHECK_INT(node->out.dtype, cases[i].fail_stage == 3 ? VAR_TYPE_INT : VAR_TYPE_DOUBLE);
        if (cases[i].fail_stage == 3) DCG_CHECK_INT(node->out.value.as_int, 7);

        t_free(node);
    }
}

static void test_a_failing_hook_stops_the_walk_at_that_node(void) {
    dcg_root_node* root = c_dcg_node_new_root(NULL, NULL);
    dcg_node*      node = t_binary(DCG_OP_GT, 2.0, 1.0);
    dcg_node*      leaf = dcg_t_node_action(DCG_NODE_LONGACTION, "below");
    t_hook_log     log  = {0};
    log.fail_stage      = 1; /* the pre hook refuses */
    t_hook_install(node, &log, true, false, false);

    t_attach(node, leaf, DCG_TRUE_CONDITION);
    t_attach(&root->base, node, DCG_NO_CONDITION);

    DCG_CHECK_INT(c_dcg_root_node_eval(root), DCG_ERR_BUSY);
    DCG_CHECK(root->eval_path.failed == node);
    DCG_CHECK(root->eval_path.leaf == NULL);
    DCG_CHECK_INT(node->eval_ctx.err_code, DCG_ERR_BUSY);
    DCG_CHECK_INT(node->eval_ctx.stage, DCG_EVAL_STAGE_NONE);
    DCG_CHECK_INT(node->eval_ctx.visits, 1); /* it was visited; it did not get through */
    DCG_CHECK_INT(leaf->eval_ctx.visits, 0);
    t_trace_path("hook refused", &root->eval_path);

    c_dcg_node_teardown_root(&root->base);
}

static void test_the_evaluated_event_carries_the_run_id(void) {
    t_event_count     = 0;
    t_event_last_seq  = 0;
    t_event_last_node = NULL;

    dcg_root_node* root = c_dcg_node_new_root(NULL, NULL);
    dcg_node*      leaf = dcg_t_node_action(DCG_NODE_NOACTION, "leaf");
    t_attach(&root->base, leaf, DCG_NO_CONDITION);

    uintptr_t callback_id = 0;
    DCG_CHECK_INT(c_dcg_node_register_callback(leaf, t_on_event, NULL, &callback_id), DCG_OK);

    DCG_CHECK_INT(c_dcg_root_node_eval(root), DCG_OK);
    DCG_CHECK_INT(t_event_count, 1);
    DCG_CHECK(t_event_last_node == leaf);
    DCG_CHECK(t_event_last_seq == root->eval_path.seq_id);
    DCG_CHECK(t_event_last_seq == leaf->eval_ctx.eval_seq_id);

    /* A node the walk does not reach is never announced, so a listener that
     * hears an event is hearing about a value that was really produced. */
    dcg_root_node* other_root = c_dcg_node_new_root(NULL, NULL);
    dcg_node*      branch     = t_binary(DCG_OP_LT, 2.0, 1.0); /* evaluates false */
    dcg_node*      reached    = dcg_t_node_action(DCG_NODE_NOACTION, "reached");
    dcg_node*      skipped    = dcg_t_node_action(DCG_NODE_NOACTION, "skipped");
    uintptr_t      skipped_id = 0;

    t_event_count = 0;
    DCG_CHECK_INT(c_dcg_node_register_callback(skipped, t_on_event, NULL, &skipped_id), DCG_OK);
    t_attach(branch, reached, DCG_TRUE_CONDITION);
    t_attach(branch, skipped, DCG_FALSE_CONDITION);
    t_attach(&other_root->base, branch, DCG_NO_CONDITION);

    DCG_CHECK_INT(c_dcg_root_node_eval(other_root), DCG_OK);
    DCG_CHECK(other_root->eval_path.leaf == skipped); /* the false arm is the one reached */
    DCG_CHECK_INT(t_event_count, 1);
    DCG_CHECK(t_event_last_node == skipped);
    DCG_CHECK_INT(reached->eval_ctx.visits, 0);
    c_dcg_node_teardown_root(&other_root->base);

    c_dcg_node_teardown_root(&root->base);
}

static void test_a_walk_from_a_middle_node_records_into_the_path_it_is_given(void) {
    dcg_mapping_lgroup* store = dcg_t_mapping_lgroup(4, "sub");
    dcg_node*           long_ = NULL;
    dcg_node*           root  = t_deep_tree(store, &long_);

    DCG_CHECK_INT(c_dcg_mapping_lgroup_set_double(store, "signal", 6, 5.0), DCG_OK);

    dcg_node* middle = root->children; /* "signal > 0", two levels above the leaf */

    /* A record of its own, sized for what a walk from there can need. */
    dcg_node_eval_path* path = c_dcg_node_eval_path_new(c_dcg_node_height(middle) + 1, NULL);
    DCG_CHECK(path != NULL);
    DCG_CHECK_INT(c_dcg_node_eval_graph(middle, path), DCG_OK);
    t_trace_path("from the middle", path);

    DCG_CHECK_INT(path->n_nodes, 3);
    DCG_CHECK(path->node[0] == middle);
    DCG_CHECK(path->leaf == long_);
    DCG_CHECK_INT(middle->eval_ctx.depth, 0); /* the walk's top is depth 0, wherever it started */

    /* A walk from a leaf: one node, and it lands on it. */
    dcg_node_eval_path* leaf_path = c_dcg_node_eval_path_new(0, NULL); /* no size given: the default */
    DCG_CHECK(leaf_path != NULL);
    DCG_CHECK_INT(c_dcg_node_eval_graph(long_, leaf_path), DCG_OK);
    DCG_CHECK_INT(leaf_path->n_nodes, 1);
    DCG_CHECK(leaf_path->leaf == long_);

    c_dcg_node_eval_path_free(path);
    c_dcg_node_eval_path_free(leaf_path);
    c_dcg_node_teardown_root(root);
    c_dcg_mapping_lgroup_free(store);
}

static void test_a_graph_that_cannot_resolve_reports_the_node_that_failed(void) {
    /* The store is never filled, so the topmost branch cannot produce a value:
     * the walk stops on it and the record names it. */
    dcg_mapping_lgroup* store = dcg_t_mapping_lgroup(4, "empty");
    dcg_node*           long_ = NULL;
    dcg_node*           root  = t_deep_tree(store, &long_);

    DCG_CHECK_INT(c_dcg_root_node_eval((dcg_root_node*) root), DCG_ERR_UNBOUND);
    const dcg_node_eval_path* path = &((dcg_root_node*) root)->eval_path;
    t_trace_path("unresolved", path);

    DCG_CHECK_INT(path->code, DCG_ERR_UNBOUND);
    DCG_CHECK(path->failed == root->children);
    DCG_CHECK(path->leaf == NULL);
    DCG_CHECK_INT(path->n_nodes, 2); /* the root, and the branch that refused */
    DCG_CHECK_INT(long_->eval_ctx.visits, 0); /* nothing below the failure was reached */

    c_dcg_node_teardown_root(root);
    c_dcg_mapping_lgroup_free(store);
}

// ========== The path record ==========

static void test_a_root_records_into_the_room_it_made_once(void) {
    dcg_root_node* root = c_dcg_node_new_root(NULL, NULL);
    dcg_node*      leaf = dcg_t_node_action(DCG_NODE_NOACTION, "leaf");
    t_attach(&root->base, leaf, DCG_NO_CONDITION);

    DCG_CHECK(root->eval_path.node == NULL); /* nothing walked yet: no blocks at all */

    DCG_CHECK_INT(c_dcg_root_node_eval(root), DCG_OK);
    DCG_CHECK_INT(root->eval_path.n_nodes, 2);
    DCG_CHECK_INT(root->eval_path.capacity, 2); /* the height of the graph, plus the root */
    dcg_node** block = root->eval_path.node;

    /* Walked again and again, the record is emptied and refilled - the room it
     * made is the room it keeps. */
    for (int i = 0; i < 40; i++) DCG_CHECK_INT(c_dcg_root_node_eval(root), DCG_OK);
    DCG_CHECK_INT(root->eval_path.n_nodes, 2);
    DCG_CHECK(root->eval_path.node == block);

    c_dcg_node_teardown_root(&root->base); /* the record is nested under it: one free releases both */
}

static void test_a_root_records_a_walk_as_deep_as_the_graph(void) {
    /* Nineteen nested branches: twenty-one nodes on the record, one per level,
     * which is exactly what the root made room for. */
    dcg_root_node* root  = c_dcg_node_new_root(NULL, NULL);
    dcg_node*      inner = dcg_t_node_action(DCG_NODE_NOACTION, "end");

    for (size_t i = 0; i < 19; i++) {
        dcg_node* step = t_binary(DCG_OP_GT, 2.0, 1.0);
        t_attach(step, inner, DCG_TRUE_CONDITION);
        inner = step;
    }
    t_attach(&root->base, inner, DCG_NO_CONDITION);

    DCG_CHECK_INT(c_dcg_node_height(&root->base), 20);

    DCG_CHECK_INT(c_dcg_root_node_eval(root), DCG_OK);
    DCG_CHECK_INT(root->eval_path.capacity, 21);
    DCG_CHECK_INT(root->eval_path.n_nodes, 21);
    DCG_CHECK_INT(root->eval_path.leaf->ntype, DCG_NODE_NOACTION);

    size_t steps = 0;
    for (size_t i = 0; i < root->eval_path.n_nodes; i++) {
        if (root->eval_path.node[i]->ntype == DCG_NODE_BINARY) steps++;
    }
    DCG_CHECK_INT(steps, 19);
    DCG_CHECK(root->eval_path.node[0] == &root->base);
    DCG_CHECK_INT(root->eval_path.eval_val[0].dtype, VAR_TYPE_BOOL); /* the root's own true */
    t_trace_path("deep chain", &root->eval_path);

    c_dcg_node_teardown_root(&root->base);
}

static void test_a_record_too_small_grows_to_hold_the_walk(void) {
    /* A caller that sized a record too small still gets the whole walk: a record
     * that silently kept only its last entries would be a record of a different
     * walk, and the leaf alone does not say how the walk got to it. */
    dcg_root_node* root = c_dcg_node_new_root(NULL, NULL);
    dcg_node*      end  = dcg_t_node_action(DCG_NODE_NOACTION, "end");
    dcg_node*      top  = end;

    for (size_t i = 0; i < 6; i++) {
        dcg_node* step = t_binary(DCG_OP_GT, 2.0, 1.0);
        t_attach(step, top, DCG_TRUE_CONDITION);
        top = step;
    }
    t_attach(&root->base, top, DCG_NO_CONDITION); /* an eight-node walk */

    dcg_node_eval_path* path = c_dcg_node_eval_path_new(3, NULL);
    DCG_CHECK(path != NULL);
    DCG_CHECK_INT(path->capacity, 3);

    DCG_CHECK_INT(c_dcg_node_eval_graph(&root->base, path), DCG_OK);
    t_trace_path("three of eight, grown", path);

    DCG_CHECK(path->capacity >= 8); /* it took what it needed, growing as it went */
    DCG_CHECK_INT(path->n_nodes, 8);
    DCG_CHECK(path->node[0] == &root->base); /* the walk from the start, nothing lost off the front */
    DCG_CHECK_INT(path->code, DCG_OK);
    DCG_CHECK_INT(path->leaf->ntype, DCG_NODE_NOACTION);

    /* The same record, used again for a walk that fits: emptied first, so
     * nothing of the last walk's count is left standing. */
    DCG_CHECK_INT(c_dcg_node_eval_graph(end, path), DCG_OK);
    DCG_CHECK_INT(path->n_nodes, 1);
    DCG_CHECK(path->leaf == end);

    c_dcg_node_eval_path_free(path);
    c_dcg_node_teardown_root(&root->base);
}

static void test_a_record_with_no_room_is_refused_up_front(void) {
    dcg_node* leaf = dcg_t_node_action(DCG_NODE_NOACTION, "leaf");

    /* A record nothing made room for cannot be written to, so a walk asked to
     * fill one is refused before it starts - rather than walked past and
     * reported as a value with no record behind it. */
    dcg_node_eval_path empty;
    memset(&empty, 0, sizeof(empty));
    DCG_CHECK_INT(c_dcg_eval_path_append(&empty, leaf), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(empty.n_nodes, 0);
    DCG_CHECK_INT(c_dcg_node_eval_graph(leaf, &empty), DCG_ERR_INVALID_ARG);

    /* No record asked for is no refusal: the walk runs and reports its code. */
    DCG_CHECK_INT(c_dcg_node_eval_graph(leaf, NULL), DCG_OK);

    t_free(leaf);
}

int main(void) {
    (void) printf("test_c_eval\n");

    DCG_RUN(test_a_literal_evaluates_to_itself);
    DCG_RUN(test_one_node_keeps_the_value_it_produced);
    DCG_RUN(test_the_dry_run_hands_the_value_out);
    DCG_RUN(test_one_node_leaves_the_run_state_alone);

    DCG_RUN(test_a_variable_reads_the_slot_it_reflects);
    DCG_RUN(test_an_operand_of_an_operand_is_still_live);
    DCG_RUN(test_a_variable_that_reflects_nothing_says_so);
    DCG_RUN(test_a_read_is_resolved_to_the_type_the_entry_holds);
    DCG_RUN(test_a_retyped_entry_is_resolved_again);
    DCG_RUN(test_a_frozen_stores_read_is_resolved_by_its_first_evaluation);
    DCG_RUN(test_a_reserved_entry_is_answered_after_the_freeze);
    DCG_RUN(test_a_read_built_before_a_growth_reads_where_the_entry_moved_to);
    DCG_RUN(test_the_operators_evaluate_by_their_operands);
    DCG_RUN(test_the_unary_operators);
    DCG_RUN(test_an_operand_outside_the_domain_is_refused);
    DCG_RUN(test_an_expression_over_a_read_is_evaluated_live);
    DCG_RUN(test_a_ternary_reads_only_the_arm_it_takes);
    DCG_RUN(test_a_call_has_no_evaluation);
    DCG_RUN(test_an_action_evaluates_to_itself);

    DCG_RUN(test_the_walk_lands_on_the_leaf_the_values_select);
    DCG_RUN(test_the_true_arm_is_taken_first);
    DCG_RUN(test_the_else_arm_is_the_fallback);
    DCG_RUN(test_a_branch_that_matches_nothing_reports_no_match);
    DCG_RUN(test_a_breakpoint_passes_through_and_a_dangling_one_ends_the_walk);
    DCG_RUN(test_the_walk_writes_the_bookkeeping_of_every_node_it_visits);
    DCG_RUN(test_a_cached_node_keeps_the_value_it_holds);
    DCG_RUN(test_skip_children_ends_the_walk_where_it_stands);
    DCG_RUN(test_a_hook_is_installed_once_and_its_data_has_to_agree);
    DCG_RUN(test_a_record_snapshots_the_value_a_node_reads_as);
    DCG_RUN(test_the_hooks_run_around_the_value_in_order);
    DCG_RUN(test_a_failing_hook_stops_the_node_and_names_the_stage);
    DCG_RUN(test_a_failing_hook_stops_the_walk_at_that_node);
    DCG_RUN(test_the_evaluated_event_carries_the_run_id);
    DCG_RUN(test_a_walk_from_a_middle_node_records_into_the_path_it_is_given);
    DCG_RUN(test_a_graph_that_cannot_resolve_reports_the_node_that_failed);

    DCG_RUN(test_a_root_records_into_the_room_it_made_once);
    DCG_RUN(test_a_root_records_a_walk_as_deep_as_the_graph);
    DCG_RUN(test_a_record_too_small_grows_to_hold_the_walk);
    DCG_RUN(test_a_record_with_no_room_is_refused_up_front);

    DCG_SUMMARY("test_c_eval");
    return dcg_test_failures == 0 ? 0 : 1;
}
