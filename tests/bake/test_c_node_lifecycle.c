/*
 * c_node.h - lifecycle: typed construction, the zeroed-buf contract, freeing
 * a node (which lets its children go) against tearing a graph down (which
 * frees every node of it), and the clean-keeps-bindings rule.
 */

#include <decision_graph/decision_tree/bake/c_node.h>

#include <decision_graph/decision_tree/bake/c_action.h>
#include <decision_graph/decision_tree/bake/c_collection.h>
#include <decision_graph/decision_tree/bake/c_const.h>
#include <decision_graph/decision_tree/bake/c_expr.h>

#include <decision_graph/decision_tree/bake/c_hierarchy.h>

#include "test_util.h"

/** A hook that only marks itself as called, for binding-survival checks. */
static int lifetime_hook(dcg_node* node, void* user_data) {
    (void) node;
    if (user_data) (*(int*) user_data)++;
    return DCG_OK;
}

/** A mutation observer that only counts its invocations. */
static void lifetime_callback(dcg_node_event event, dcg_node* self, dcg_node* subject, uint64_t seq_id, void* user_data) {
    (void) event;
    (void) self;
    (void) subject;
    (void) seq_id;
    if (user_data) (*(int*) user_data)++;
}

static void test_generic_init_and_new(void) {
    /* A node owns its repr, so its buf must be an allocator block: the repr
     * copy is nested under it. */
    dcg_node* node = c_dcg_node_new(DCG_NODE_LIST, "a + b", NULL);
    DCG_CHECK(node != NULL);
    DCG_CHECK_INT(node->ntype, DCG_NODE_LIST);
    DCG_CHECK_STR(node->repr, "a + b");
    DCG_CHECK(node->parent == NULL);
    DCG_CHECK(node->children == NULL);
    DCG_CHECK(node->condition_to_parent == NULL); /* means NO_CONDITION */
    DCG_CHECK_INT(node->out.dtype, VAR_TYPE_RAW_PTR);
    c_dcg_node_free(node);

    dcg_node* heap = c_dcg_node_new(DCG_NODE_LIST, "items", NULL);
    DCG_CHECK(heap != NULL);
    DCG_CHECK_INT(heap->ntype, DCG_NODE_LIST);
    DCG_CHECK(c_dcg_node_is_root(heap));
    DCG_CHECK(c_dcg_node_is_leaf(heap));
    c_dcg_node_free(heap);

    DCG_CHECK_INT(c_dcg_node_init(NULL, DCG_NODE_ROOT, "x"), DCG_ERR_INVALID_ARG);
    c_dcg_node_dealloc(NULL);
    c_dcg_node_free(NULL);
}

static void test_value_is_replaceable(void) {
    dcg_node* node = dcg_t_node_double("first", 1.0);
    DCG_CHECK(node->out.value.as_double == 1.0);

    /* Replacing the payload leaves the identity alone: the kind and the repr
     * belong to the node, not to the value it holds. */
    DCG_CHECK_INT(c_dcg_node_const_set((dcg_constant_node*) node, dcg_t_var_string("s")), DCG_OK);
    DCG_CHECK_INT(node->ntype, DCG_NODE_DOUBLE);
    DCG_CHECK_STR(node->repr, "first");
    DCG_CHECK_INT(node->out.dtype, VAR_TYPE_STRING);
    DCG_CHECK(node->eval_ctx.eval_fn == NULL);

    /* An explicit retitle still wins. */
    DCG_CHECK_INT(c_dcg_node_set_repr(node, "second"), DCG_OK);
    DCG_CHECK_STR(node->repr, "second");

    c_dcg_node_free(node);
}

static void test_typed_constructors(void) {
    dcg_node* node = NULL;

    node = dcg_t_node_bool("t", true);
    DCG_CHECK_INT(node->ntype, DCG_NODE_TRUE);
    DCG_CHECK(c_dcg_var_as_bool(&node->out));
    c_dcg_node_free(node);

    node = dcg_t_node_bool("f", false);
    DCG_CHECK_INT(node->ntype, DCG_NODE_FALSE);
    DCG_CHECK(!c_dcg_var_as_bool(&node->out));
    c_dcg_node_free(node);

    node = dcg_t_node_double("d", 1.5);
    DCG_CHECK_INT(node->ntype, DCG_NODE_DOUBLE);
    DCG_CHECK(node->out.value.as_double == 1.5);
    c_dcg_node_free(node);

    node = dcg_t_node_int("i", -3);
    DCG_CHECK_INT(node->ntype, DCG_NODE_INT);
    DCG_CHECK_INT(node->out.value.as_int, -3);
    c_dcg_node_free(node);

    node = dcg_t_node_string("s", "payload");
    DCG_CHECK_INT(node->ntype, DCG_NODE_STRING);
    DCG_CHECK_STR(c_dcg_var_as_string(&node->out), "payload");
    c_dcg_node_free(node);

    dcg_constant_node* constant = c_dcg_node_new_const_value("c", dcg_t_var_offset(9), NULL);
    DCG_CHECK(constant != NULL);
    DCG_CHECK_INT(constant->base.ntype, DCG_NODE_CONST);
    DCG_CHECK_INT(constant->base.out.dtype, VAR_TYPE_OFFSET);
    c_dcg_node_free_const(constant);

    /* An expression is built over its inputs, which is what sizes it. */
    dcg_constant_node*   lhs    = c_dcg_node_new_const_double(1.0, NULL);
    dcg_expression_node* binary = c_dcg_node_new_expr_binary(DCG_OP_ADD, &lhs->base, &lhs->base, NULL);
    DCG_CHECK(binary != NULL);
    DCG_CHECK_INT(binary->base.ntype, DCG_NODE_BINARY);
    DCG_CHECK_INT(binary->op, DCG_OP_ADD);
    DCG_CHECK_INT(binary->n_args, 2);
    c_dcg_node_free_expr(binary);
    c_dcg_node_free_const(lhs);

    dcg_expression_node* ternary = c_dcg_node_new_expr(3, DCG_NODE_TERNARY, NULL);
    DCG_CHECK_INT(ternary->base.ntype, DCG_NODE_TERNARY);
    c_dcg_node_free_expr(ternary);

    /* A mapping carries a payload the base header cannot see, so only the
     * dispatcher - the free that reads the kind - releases all of it. */
    node = dcg_t_node_collection(DCG_NODE_MAPPING, "map");
    DCG_CHECK_INT(node->ntype, DCG_NODE_MAPPING);
    DCG_CHECK(c_dcg_node_type_is_collection(node->ntype));
    c_dcg_node_free_generic(node);

    node = dcg_t_node_collection(DCG_NODE_LIST, "list");
    DCG_CHECK_INT(node->ntype, DCG_NODE_LIST);
    c_dcg_node_free(node);

    node = dcg_t_node_action(DCG_NODE_NOACTION, "noop");
    DCG_CHECK_INT(node->ntype, DCG_NODE_NOACTION);
    DCG_CHECK(c_dcg_node_type_is_action(node->ntype));
    c_dcg_node_free(node);

    node = dcg_t_node_action(DCG_NODE_LONGACTION, "long");
    DCG_CHECK_INT(node->ntype, DCG_NODE_LONGACTION);
    c_dcg_node_free(node);

    node = dcg_t_node_action(DCG_NODE_SHORTACTION, "short");
    DCG_CHECK_INT(node->ntype, DCG_NODE_SHORTACTION);
    c_dcg_node_free(node);

    node = dcg_t_node_action(DCG_NODE_CANCELACTION, "cancel");
    DCG_CHECK_INT(node->ntype, DCG_NODE_CANCELACTION);
    c_dcg_node_free(node);

    node = dcg_t_node_action(DCG_NODE_CLEARACTION, "clear");
    DCG_CHECK_INT(node->ntype, DCG_NODE_CLEARACTION);
    c_dcg_node_free(node);

    node = dcg_t_node_placeholder("holder");
    DCG_CHECK_INT(node->ntype, DCG_NODE_PLACEHOLDER);
    DCG_CHECK_INT(node->autogen, true);
    c_dcg_node_free(node);

    node = dcg_t_node_root("Entry Point");
    DCG_CHECK_INT(node->ntype, DCG_NODE_ROOT);
    DCG_CHECK(c_dcg_node_type_is_special(node->ntype));
    DCG_CHECK(c_dcg_var_as_bool(&node->out));
    c_dcg_node_free(node);

    node = dcg_t_node_breakpoint("bp");
    DCG_CHECK_INT(node->ntype, DCG_NODE_BREAKPOINT);
    DCG_CHECK(node->eval_ctx.flags & DCG_EVAL_FLAG_BREAKPOINT);
    c_dcg_node_free(node);
}

static void test_action_kinds(void) {
    /* The action family is built by c_action.h: the generic constructor takes a
     * kind and everything the node carries, the variants are the shorthand. */
    int payload = 7;

    dcg_action_node* action = c_dcg_node_new_action(DCG_NODE_NOACTION, "NOACTION", true, 0, &payload, NULL);
    DCG_CHECK(action != NULL);
    DCG_CHECK_INT(action->base.ntype, DCG_NODE_NOACTION);
    DCG_CHECK_STR(action->base.repr, "NOACTION"); /* the caller names the kind */
    DCG_CHECK(action->auto_connect);
    DCG_CHECK_INT(action->sig, 0);              /* the kind decides nothing */
    DCG_CHECK(action->action_data == &payload); /* borrowed, not owned */
    dcg_t_trace_node("action NOACTION", &action->base);
    c_dcg_node_free(&action->base);

    action = c_dcg_node_new_action(DCG_NODE_LONGACTION, "go long", false, 1, NULL, NULL);
    DCG_CHECK_INT(action->base.ntype, DCG_NODE_LONGACTION);
    DCG_CHECK_STR(action->base.repr, "go long"); /* or given one */
    DCG_CHECK(!action->auto_connect);
    DCG_CHECK_INT(action->sig, 1); /* a long is +1, as the capi's class defaults to */
    DCG_CHECK(action->action_data == NULL);
    c_dcg_node_free(&action->base);

    /* The signal is the caller's to state. */
    action = c_dcg_node_new_action(DCG_NODE_SHORTACTION, NULL, true, -5, NULL, NULL);
    DCG_CHECK_INT(action->base.ntype, DCG_NODE_SHORTACTION);
    DCG_CHECK_INT(action->sig, -5); /* the node carries what it is given */
    c_dcg_node_free(&action->base);

    /* A trade states its kind and nothing else: the signal and the repr follow
     * the kind, and a kind that does not trade is refused. */
    action = c_dcg_node_new_action_trade(DCG_NODE_LONGACTION, true, NULL);
    DCG_CHECK(action != NULL);
    DCG_CHECK_INT(action->base.ntype, DCG_NODE_LONGACTION);
    DCG_CHECK_STR(action->base.repr, "LONGACTION");
    DCG_CHECK(action->auto_connect);
    DCG_CHECK_INT(action->sig, 1);
    dcg_t_trace_node("trade LONGACTION", &action->base);
    c_dcg_node_free(&action->base);

    action = c_dcg_node_new_action_trade(DCG_NODE_SHORTACTION, false, NULL);
    DCG_CHECK_INT(action->base.ntype, DCG_NODE_SHORTACTION);
    DCG_CHECK_INT(action->sig, -1);
    c_dcg_node_free(&action->base);

    action = c_dcg_node_new_action_trade(DCG_NODE_CANCELACTION, true, NULL);
    DCG_CHECK_INT(action->base.ntype, DCG_NODE_CANCELACTION);
    DCG_CHECK_INT(action->sig, 0); /* a cancel closes what is open, it does not take a side */
    c_dcg_node_free(&action->base);

    /* Only the kinds that trade are accepted. */
    DCG_CHECK(c_dcg_node_new_action_trade(DCG_NODE_CLEARACTION, true, NULL) == NULL);
    DCG_CHECK(c_dcg_node_new_action_trade(DCG_NODE_NOACTION, true, NULL) == NULL);
    DCG_CHECK(c_dcg_node_new_action_trade(DCG_NODE_PLACEHOLDER, true, NULL) == NULL);
    DCG_CHECK(c_dcg_node_new_action_trade(DCG_NODE_ACTION, true, NULL) == NULL); /* the family head is not a trade */
    DCG_CHECK(c_dcg_node_new_action_trade(DCG_NODE_DOUBLE, true, NULL) == NULL);

    action = c_dcg_node_new_action_clear(true, NULL);
    DCG_CHECK_INT(action->base.ntype, DCG_NODE_CLEARACTION);
    DCG_CHECK_STR(action->base.repr, "CLEARACTION");
    c_dcg_node_free(&action->base);

    action = c_dcg_node_new_action_placeholder(false, NULL);
    DCG_CHECK_INT(action->base.ntype, DCG_NODE_PLACEHOLDER);
    DCG_CHECK(action->base.autogen); /* a placeholder is auto-generated by definition */
    DCG_CHECK_STR(action->base.repr, DCG_DEF_REPR_PLACEHOLDER);
    c_dcg_node_free(&action->base);

    /* A kind that is not an action is refused, and nothing is allocated. */
    DCG_CHECK(c_dcg_node_new_action(DCG_NODE_DOUBLE, NULL, true, 0, NULL, NULL) == NULL);
    DCG_CHECK(c_dcg_node_new_action(DCG_NODE_BINARY, NULL, true, 0, NULL, NULL) == NULL);

    /* An action carries fields the base header cannot hold, so the base
     * constructor refuses the kind. */
    DCG_CHECK(!c_dcg_node_type_is_flat(DCG_NODE_NOACTION));
    DCG_CHECK(c_dcg_node_new(DCG_NODE_NOACTION, "x", NULL) == NULL);
}

static void test_special_kinds(void) {
    /* The root carries what a walk starts with: the contexts it does not
     * inherit, the depth it may reach, and the empty record of its path. */
    dcg_root_node* root = c_dcg_node_new_root(NULL);
    DCG_CHECK(root != NULL);
    DCG_CHECK_INT(root->base.ntype, DCG_NODE_ROOT);
    DCG_CHECK(c_dcg_var_as_bool(&root->base.out)); /* the entry is taken */
    DCG_CHECK_STR(root->base.repr, DCG_DEF_REPR_ROOT);
    dcg_t_trace_node("root", &root->base);
    DCG_CHECK(!root->inherit_contexts); /* the capi's default */
    DCG_CHECK_INT(root->max_depth, 0);  /* 0 walks the whole graph */
    DCG_CHECK(root->eval_path.node == NULL);
    DCG_CHECK(root->eval_path.eval_val == NULL);
    DCG_CHECK_INT(root->eval_path.n_nodes, 0);
    DCG_CHECK_INT(root->eval_path.capacity, 0);

    root->inherit_contexts = true; /* the builder owns the choice */
    DCG_CHECK(root->inherit_contexts);
    c_dcg_node_free(&root->base);

    /* The breakpoint carries the flag an evaluator stops on, and the two fields
     * its group logic uses. */
    dcg_breakpoint_node* breakpoint = c_dcg_node_new_breakpoint(NULL);
    DCG_CHECK(breakpoint != NULL);
    DCG_CHECK_INT(breakpoint->base.ntype, DCG_NODE_BREAKPOINT);
    DCG_CHECK(breakpoint->base.eval_ctx.flags & DCG_EVAL_FLAG_BREAKPOINT);
    DCG_CHECK_STR(breakpoint->base.repr, DCG_DEF_REPR_BREAKPOINT);
    DCG_CHECK(!breakpoint->await_connection); /* nothing has been left yet */
    DCG_CHECK(breakpoint->break_from == NULL);
    c_dcg_node_free(&breakpoint->base);

    /* Both carry fields the base header cannot hold, so the base constructor
     * refuses the kinds and the family sizes the block. */
    DCG_CHECK(!c_dcg_node_type_is_flat(DCG_NODE_ROOT));
    DCG_CHECK(!c_dcg_node_type_is_flat(DCG_NODE_BREAKPOINT));
    DCG_CHECK(c_dcg_node_new(DCG_NODE_ROOT, "x", NULL) == NULL);
    DCG_CHECK(c_dcg_node_new(DCG_NODE_BREAKPOINT, "x", NULL) == NULL);
}

static void test_type_utilities(void) {
    DCG_CHECK_STR(c_dcg_node_type_name(DCG_NODE_ROOT), "ROOT");
    DCG_CHECK_STR(c_dcg_node_type_name(DCG_NODE_DOUBLE), "DOUBLE");
    DCG_CHECK_STR(c_dcg_node_type_name(DCG_NODE_PLACEHOLDER), "PLACEHOLDER");
    DCG_CHECK_STR(c_dcg_node_type_name((dcg_node_type) 0x7fff), "UNKNOWN");

    DCG_CHECK_STR(c_dcg_op_code_name(DCG_OP_ADD), "ADD");
    DCG_CHECK_STR(c_dcg_op_code_name(DCG_OP_LE), "LE");
    DCG_CHECK_STR(c_dcg_op_code_name(DCG_OP_NONE), "NONE");
    DCG_CHECK_STR(c_dcg_op_code_name((dcg_op_code) 0x7fff), "UNKNOWN");

    /* Families are the high nibble: masked, a variant equals its family head. */
    DCG_CHECK_INT(DCG_NODE_TRUE & DCG_NODE_FAMILY_MASK, DCG_NODE_CONST);
    DCG_CHECK_INT(DCG_NODE_BINARY & DCG_NODE_FAMILY_MASK, DCG_NODE_OP);
    DCG_CHECK_INT(DCG_NODE_MAPPING & DCG_NODE_FAMILY_MASK, DCG_NODE_COLLECTION);
    DCG_CHECK_INT(DCG_NODE_LONGACTION & DCG_NODE_FAMILY_MASK, DCG_NODE_ACTION);
    DCG_CHECK_INT(DCG_NODE_ROOT & DCG_NODE_FAMILY_MASK, DCG_NODE_SPECIAL);

    DCG_CHECK(c_dcg_node_type_is_const(DCG_NODE_TRUE));
    DCG_CHECK(c_dcg_node_type_is_op(DCG_NODE_UNARY));
    DCG_CHECK(c_dcg_node_type_is_collection(DCG_NODE_LIST));
    DCG_CHECK(c_dcg_node_type_is_action(DCG_NODE_NOACTION));
    DCG_CHECK(c_dcg_node_type_is_special(DCG_NODE_BREAKPOINT));

    DCG_CHECK_INT(c_dcg_node_type_arity(DCG_NODE_UNARY), 1);
    DCG_CHECK_INT(c_dcg_node_type_arity(DCG_NODE_BINARY), 2);
    DCG_CHECK_INT(c_dcg_node_type_arity(DCG_NODE_TERNARY), 3);
    DCG_CHECK_INT(c_dcg_node_type_arity(DCG_NODE_CALL), 0);
    DCG_CHECK_INT(c_dcg_node_type_arity(DCG_NODE_DOUBLE), 0);

    DCG_CHECK_INT(c_dcg_op_code_node_type(DCG_OP_NEG), DCG_NODE_UNARY);
    DCG_CHECK_INT(c_dcg_op_code_node_type(DCG_OP_NOT), DCG_NODE_UNARY);
    DCG_CHECK_INT(c_dcg_op_code_node_type(DCG_OP_ADD), DCG_NODE_BINARY);
    DCG_CHECK_INT(c_dcg_op_code_node_type(DCG_OP_EQ), DCG_NODE_BINARY);
    DCG_CHECK_INT(c_dcg_op_code_node_type(DCG_OP_AND), DCG_NODE_BINARY);
    DCG_CHECK_INT(c_dcg_op_code_node_type(DCG_OP_GETITEM), DCG_NODE_BINARY);
    DCG_CHECK_INT(c_dcg_op_code_node_type(DCG_OP_NONE), DCG_NODE_OP);
}

static void test_seq_id(void) {
    dcg_node* a = dcg_t_node_collection(DCG_NODE_LIST, "a");
    dcg_node* b = dcg_t_node_collection(DCG_NODE_LIST, "b");

    uint64_t  id_a = c_dcg_node_gen_seq_id(a);
    uint64_t  id_b = c_dcg_node_gen_seq_id(b);
    DCG_CHECK(id_a != 0);
    DCG_CHECK(id_a != id_b);                       /* distinct per node */
    DCG_CHECK_INT(c_dcg_node_gen_seq_id(a), id_a); /* stable per node */
    DCG_CHECK_INT(c_dcg_node_gen_seq_id(NULL), 0);

    c_dcg_node_free(a);
    c_dcg_node_free(b);
}

static void test_dealloc_zeroes(void) {
    dcg_node* node                    = dcg_t_node_binary(DCG_OP_ADD, "a + b");
    node->autogen                     = true;
    ((dcg_expression_node*) node)->op = DCG_OP_ADD;

    c_dcg_node_dealloc(node);

    /* The buf is handed back zeroed, so an embedded caller can reuse it. The
     * base half is what went: the operand array lives inside the block (and
     * holds no folded copy here, which is the family free's business). */
    DCG_CHECK_INT(node->ntype, DCG_NODE_CONST);
    DCG_CHECK(node->repr == NULL);
    DCG_CHECK(node->children == NULL);
    DCG_CHECK(node->parent == NULL);
    DCG_CHECK_INT(node->flags, 0);
    DCG_CHECK_INT(node->autogen, false);
    c_ap_free(node); /* the block itself is still alive, as contracted */
}

static void test_generic_free_dispatch(void) {
    /* The dispatcher reads the kind, so every route it has must land on the free
     * of that kind - the family ones, the base-only one, and the actions, which
     * go through their own free. The sanitizer run is what proves each route
     * released everything the node owned. */
    dcg_node* node = NULL;

    node = dcg_t_node_double("d", 1.0); /* the constant family */
    c_dcg_node_free_generic(node);

    node = dcg_t_node_binary(DCG_OP_ADD, "a + b"); /* the operator family */
    c_dcg_node_free_generic(node);

    node = dcg_t_node_collection(DCG_NODE_MAPPING, "map"); /* a mapping owns its index */
    c_dcg_node_free_generic(node);

    dcg_var_t         reflected;
    dcg_variable_node* variable = NULL;

    (void) c_dcg_var_init_int(&reflected, 1);
    variable = c_dcg_node_new_var("var", &reflected, NULL); /* a variable reflects a value another node holds */
    DCG_CHECK(variable != NULL);
    c_dcg_node_free_generic(variable ? &variable->base : NULL);

    node = dcg_t_node_collection(DCG_NODE_LIST, "list"); /* a list is the base node alone */
    c_dcg_node_free_generic(node);

    node = dcg_t_node_action(DCG_NODE_LONGACTION, "long"); /* an action goes through c_dcg_node_free_action() */
    c_dcg_node_free_generic(node);

    node = dcg_t_node_placeholder("holder");
    c_dcg_node_free_generic(node);

    node = dcg_t_node_root("Entry Point"); /* the graph's own kinds */
    c_dcg_node_free_generic(node);

    node = dcg_t_node_breakpoint("bp");
    c_dcg_node_free_generic(node);

    c_dcg_node_free_generic(NULL); /* NULL-safe */

    /* A kind the dispatcher has no free for is reported on stderr and released
     * base only - it must not take another kind's route, or leak. */
    dcg_node* unknown = (dcg_node*) c_ap_alloc(sizeof(dcg_node), NULL);
    DCG_CHECK(unknown != NULL);
    DCG_CHECK_INT(c_dcg_node_init(unknown, (dcg_node_type) 0x7f00, "ghost"), DCG_OK);
    c_dcg_node_free_generic(unknown);
}

static void test_tree_teardown(void) {
    /* A whole graph is torn down from its top, and only from there: every node
     * is released leaf first, and the count says how many went. */
    dcg_node* root    = dcg_t_node_root("Entry Point");
    dcg_node* operand = dcg_t_node_binary(DCG_OP_LT, "x < 10");
    dcg_node* yes     = dcg_t_node_double("yes", 1.0);
    dcg_node* no      = dcg_t_node_double("no", 0.0);

    DCG_CHECK_INT(c_dcg_node_append(root, operand, DCG_NO_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append_auto(operand, yes), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append_auto(operand, no), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_subtree_size(root), 4);

    /* A node that still has a parent is somebody else's subtree: refused, and
     * nothing is touched. */
    DCG_CHECK_INT(c_dcg_node_teardown_root(operand), DCG_ERR_BUSY);
    DCG_CHECK_INT(c_dcg_node_subtree_size(root), 4);
    DCG_CHECK_INT(c_dcg_node_teardown_root(NULL), DCG_ERR_INVALID_ARG);

    dcg_t_trace_tree("tree(root -> x < 10 -> {yes, no})", root);
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 4); /* root, operand, yes, no */
}

static void test_freeing_a_node_lets_its_children_go(void) {
    /* c_dcg_node_free() is the free of a NODE, not of a subtree: the children
     * are unlinked and stay alive, each with its own subtree, and tearing a
     * graph down is c_dcg_node_teardown_root's job. */
    dcg_node* parent     = dcg_t_node_collection(DCG_NODE_LIST, "parent");
    dcg_node* child      = dcg_t_node_collection(DCG_NODE_LIST, "child");
    dcg_node* grandchild = dcg_t_node_double("grandchild", 1.0);

    DCG_CHECK_INT(c_dcg_node_append(parent, child, DCG_NO_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append_auto(child, grandchild), DCG_OK);

    c_dcg_node_free(parent);

    /* The child was let go, not freed: parentless, edge dropped, subtree
     * intact. */
    DCG_CHECK(child->parent == NULL);
    DCG_CHECK(child->next_sibling == NULL);
    DCG_CHECK(child->prev_sibling == NULL);
    DCG_CHECK(c_dcg_condition_is_none(child->condition_to_parent));
    DCG_CHECK_INT(c_dcg_node_child_count(child), 1);
    DCG_CHECK(c_dcg_node_first_child(child) == grandchild);
    DCG_CHECK_INT(c_dcg_node_subtree_size(child), 2);

    DCG_CHECK_INT(c_dcg_node_teardown_root(child), 2); /* child + grandchild */
}

static void test_freeing_a_child_detaches_it(void) {
    dcg_node* parent = dcg_t_node_collection(DCG_NODE_LIST, "parent");
    dcg_node* first  = dcg_t_node_double("first", 1.0);
    dcg_node* second = dcg_t_node_double("second", 2.0);
    dcg_node* third  = dcg_t_node_double("third", 3.0);

    DCG_CHECK_INT(c_dcg_node_append_auto(parent, first), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append_auto(parent, second), DCG_OK);
    /* The two binary branches are taken, so the third edge is explicit. */
    DCG_CHECK_INT(c_dcg_node_append(parent, third, DCG_ELSE_CONDITION), DCG_OK);

    /* Freeing a middle child must unlink it, leaving the list consistent. */
    c_dcg_node_free(second);
    DCG_CHECK_INT(c_dcg_node_child_count(parent), 2);
    DCG_CHECK(c_dcg_node_next_sibling(first) == third);
    DCG_CHECK(c_dcg_node_prev_sibling(third) == first);

    /* The parent is the top of what is left, so the teardown takes it and the
     * two children with it. */
    DCG_CHECK_INT(c_dcg_node_teardown_root(parent), 3);
}

static void test_freeing_a_subtree(void) {
    dcg_node* root   = dcg_t_node_root("Entry Point");
    dcg_node* branch = dcg_t_node_collection(DCG_NODE_LIST, "branch");
    dcg_node* leaf   = dcg_t_node_action(DCG_NODE_NOACTION, "leaf");

    DCG_CHECK_INT(c_dcg_node_append(root, branch, DCG_NO_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append_auto(branch, leaf), DCG_OK);

    /* Removing a branch takes its subtree with it - the branch was detached
     * first, which is what lets a mid-graph node be torn down at all... */
    DCG_CHECK_INT(c_dcg_node_remove(branch), 2); /* branch + leaf */
    DCG_CHECK(c_dcg_node_is_leaf(root));

    /* ...and the root survives with a clean slate. */
    DCG_CHECK_INT(c_dcg_node_child_count(root), 0);
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 1);
}

static void test_owned_strings(void) {
    /* The node copies its repr and its string value into nested blocks, so both
     * are released by its own teardown and the caller's text can die at once. */
    char      repr[]  = "a repr";
    char      value[] = "a value";
    dcg_node* node    = dcg_t_node_string(repr, value);

    DCG_CHECK(node != NULL);
    DCG_CHECK_STR(node->repr, "a repr");
    DCG_CHECK(c_dcg_var_as_string(&node->out) != value); /* a copy, not the caller's pointer */

    repr[0]  = 'X';
    value[0] = 'X';
    DCG_CHECK_STR(node->repr, "a repr"); /* immune to whatever the source does next */
    DCG_CHECK_STR(c_dcg_var_as_string(&node->out), "a value");

    /* Replacing the text releases the copy it replaces. */
    DCG_CHECK_INT(c_dcg_node_set_string(node, "second"), DCG_OK);
    DCG_CHECK_STR(c_dcg_var_as_string(&node->out), "second");
    DCG_CHECK_INT(c_dcg_node_set_repr(node, "second repr"), DCG_OK);
    DCG_CHECK_STR(node->repr, "second repr");

    /* A NULL repr copies nothing, so it is legal even without an allocator. */
    DCG_CHECK_INT(c_dcg_node_set_repr(node, NULL), DCG_OK);
    DCG_CHECK(node->repr == NULL);

    c_dcg_node_free(node); /* frees the remaining copies - LSan checks it */
}

static void test_owned_condition(void) {
    /* A condition linked to a node is owned by it: it becomes a nested block
     * and dies with the node, with no flag and no teardown step of its own. */
    dcg_node* node = dcg_t_node_double("d", 1.0);
    DCG_CHECK_INT(c_dcg_node_append(node, dcg_t_node_double("child", 1.0), c_dcg_edge_new(dcg_t_var_int(5), "five", NULL)), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_teardown_root(node), 2); /* the child and the adopted condition go with it */

    /* A built-in is a static object, never adopted: tearing a node that holds
     * one down must leave it intact. */
    dcg_node* builtin = dcg_t_node_double("d", 2.0);
    DCG_CHECK_INT(c_dcg_node_append(builtin, dcg_t_node_double("child", 1.0), DCG_TRUE_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_teardown_root(builtin), 2);
    DCG_CHECK(c_dcg_condition_is_true(DCG_TRUE_CONDITION));
    DCG_CHECK(DCG_TRUE_CONDITION->repr[0] != '\0'); /* still a readable built-in */

    /* The same edge moved to a replacement keeps its condition alive. */
    dcg_node* old_child = dcg_t_node_double("old", 1.0);
    dcg_node* holder    = dcg_t_node_double("holder", 0.0);
    DCG_CHECK_INT(c_dcg_node_append(holder, old_child, c_dcg_edge_new(dcg_t_var_int(7), "seven", NULL)), DCG_OK);

    dcg_node* new_child = dcg_t_node_double("new", 2.0);
    DCG_CHECK_INT(c_dcg_node_replace(old_child, new_child), DCG_OK);
    c_dcg_node_free(old_child);                                               /* the displaced node goes away... */
    DCG_CHECK(c_dcg_var_as_int(&new_child->condition_to_parent->value) == 7); /* ...the edge does not */
    DCG_CHECK_INT(c_dcg_node_teardown_root(holder), 2);                       /* holder + new_child, edge and all */
}

static void test_clean_keeps_bindings(void) {
    dcg_node* node           = dcg_t_node_collection(DCG_NODE_LIST, "branch");
    dcg_node* child          = dcg_t_node_double("child", 1.0);
    int       hook_calls     = 0;
    int       callback_calls = 0;
    uintptr_t callback_id    = 0;

    DCG_CHECK_INT(c_dcg_node_append_auto(node, child), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_add_label(node, "keep"), DCG_OK);
    node->eval_ctx.eval_fn   = lifetime_hook;
    node->eval_ctx.user_data = &hook_calls;
    DCG_CHECK_INT(c_dcg_node_register_callback(node, lifetime_callback, &callback_calls, &callback_id), DCG_OK);
    node->user_payload = (void*) 0x1234;

    DCG_CHECK_INT(c_dcg_node_clean(node), 1); /* the child's subtree went with the reset */

    /* Dropped: the subtree and the labels. */
    DCG_CHECK_INT(c_dcg_node_child_count(node), 0);
    DCG_CHECK_INT(c_dcg_node_label_count(node), 0);
    DCG_CHECK_INT(node->eval_ctx.visits, 0);
    DCG_CHECK(node->eval_ctx.run == NULL);

    /* Kept: identity, bindings and the payload slot. */
    DCG_CHECK_INT(node->ntype, DCG_NODE_LIST);
    DCG_CHECK_STR(node->repr, "branch");
    DCG_CHECK(node->eval_ctx.eval_fn == lifetime_hook);
    DCG_CHECK_INT(c_dcg_node_callback_count(node), 1);
    DCG_CHECK((void*) node->user_payload == (void*) 0x1234);

    /* The surviving hook still fires, called the way the evaluator calls it. */
    DCG_CHECK_INT(node->eval_ctx.eval_fn(node, node->eval_ctx.user_data), DCG_OK);
    DCG_CHECK_INT(hook_calls, 1);

    c_dcg_node_free(node);
}

int main(void) {
    (void) printf("test_c_node_lifecycle\n");
    DCG_RUN(test_generic_init_and_new);
    DCG_RUN(test_value_is_replaceable);
    DCG_RUN(test_typed_constructors);
    DCG_RUN(test_action_kinds);
    DCG_RUN(test_special_kinds);
    DCG_RUN(test_type_utilities);
    DCG_RUN(test_seq_id);
    DCG_RUN(test_dealloc_zeroes);
    DCG_RUN(test_generic_free_dispatch);
    DCG_RUN(test_tree_teardown);
    DCG_RUN(test_freeing_a_node_lets_its_children_go);
    DCG_RUN(test_freeing_a_child_detaches_it);
    DCG_RUN(test_freeing_a_subtree);
    DCG_RUN(test_owned_strings);
    DCG_RUN(test_owned_condition);
    DCG_RUN(test_clean_keeps_bindings);
    DCG_SUMMARY("test_c_node_lifecycle");
    return dcg_test_failures == 0 ? 0 : 1;
}
