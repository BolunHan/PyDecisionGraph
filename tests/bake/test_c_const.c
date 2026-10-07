/*
 * c_const.h - the input family, which is what a graph is fed: the literals
 * (lifecycle, typed construction, the value they stand for, the repr they
 * default to) and the variable node that reads a value out of a store.
 *
 * The variable is checked here for what it is on its own - the key it owns and
 * the reference it reads. What it does against a real group's store is checked
 * in test_c_collection, where the mapping that holds one lives.
 */

#include <decision_graph/decision_tree/bake/c_const.h>

#include <decision_graph/decision_tree/bake/c_action.h>

#include <decision_graph/decision_tree/bake/c_hierarchy.h>

#include "test_util.h"

static void test_lifecycle(void) {
    /* The explicit-kind constructor is the one that takes a repr; the typed
     * ones take only the value and infer everything else from it. */
    dcg_constant_node* node = c_dcg_node_new_const(DCG_NODE_DOUBLE, "d", NULL);
    DCG_CHECK(node != NULL);
    DCG_CHECK_INT(node->base.ntype, DCG_NODE_DOUBLE);
    DCG_CHECK_STR(node->base.repr, "d");

    /* The family struct IS the base node: the graph API walks it directly. */
    DCG_CHECK((dcg_node*) node == &node->base);
    DCG_CHECK(c_dcg_node_is_root(&node->base));
    c_dcg_node_free_const(node);

    /* An operator kind is not a constant. */
    DCG_CHECK(c_dcg_node_new_const(DCG_NODE_BINARY, "x", NULL) == NULL);

    /* A constant adds no field to the header, so the base constructor can build
     * one too - it simply has no value in it. */
    DCG_CHECK(c_dcg_node_type_is_flat(DCG_NODE_INPUT));
    DCG_CHECK(c_dcg_node_type_is_flat(DCG_NODE_DOUBLE));
    node = (dcg_constant_node*) c_dcg_node_new(DCG_NODE_INPUT, "empty", NULL);
    DCG_CHECK(node != NULL);
    DCG_CHECK(c_dcg_var_is_null(c_dcg_node_const_get(node)));
    c_dcg_node_free_const(node);
}

static void test_typed_construction(void) {
    /* Each typed constructor infers the kind and the repr from the value, so
     * there is nothing to tell it that the payload does not already say. */
    dcg_constant_node* node = c_dcg_node_new_const_bool(true, NULL);
    DCG_CHECK(node != NULL);
    DCG_CHECK_INT(node->base.ntype, DCG_NODE_TRUE);
    DCG_CHECK(c_dcg_var_as_bool(&node->base.out));
    DCG_CHECK_STR(node->base.repr, DCG_DEF_REPR_TRUE);
    dcg_t_trace_node("const TRUE", &node->base);
    c_dcg_node_free_const(node);

    node = c_dcg_node_new_const_bool(false, NULL);
    DCG_CHECK_INT(node->base.ntype, DCG_NODE_FALSE);
    DCG_CHECK(!c_dcg_var_as_bool(&node->base.out));
    DCG_CHECK_STR(node->base.repr, DCG_DEF_REPR_FALSE);
    c_dcg_node_free_const(node);

    node = c_dcg_node_new_const_double(1.5, NULL);
    DCG_CHECK_INT(node->base.ntype, DCG_NODE_DOUBLE);
    DCG_CHECK(c_dcg_var_as_double(&node->base.out) == 1.5);
    DCG_CHECK_STR(node->base.repr, "1.5");
    c_dcg_node_free_const(node);

    node = c_dcg_node_new_const_int(-3, NULL);
    DCG_CHECK_INT(node->base.ntype, DCG_NODE_INT);
    DCG_CHECK_INT(c_dcg_var_as_int(&node->base.out), -3);
    DCG_CHECK_STR(node->base.repr, "-3");
    c_dcg_node_free_const(node);

    node = c_dcg_node_new_const_string("text", NULL);
    DCG_CHECK_INT(node->base.ntype, DCG_NODE_STRING);
    DCG_CHECK_STR(c_dcg_var_as_string(&node->base.out), "text");
    DCG_CHECK_STR(node->base.repr, "text"); /* the repr is the text itself */
    dcg_t_trace_node("const STRING", &node->base);
    c_dcg_node_free_const(node);

    /* An explicit value and an explicit repr, for everything else. */
    node = c_dcg_node_new_const_value("nine", dcg_t_var_offset(9), NULL);
    DCG_CHECK(node != NULL);
    DCG_CHECK_INT(node->base.ntype, DCG_NODE_INPUT);
    DCG_CHECK_STR(node->base.repr, "nine");
    DCG_CHECK_INT(c_dcg_var_as_offset(&node->base.out), 9);
    c_dcg_node_free_const(node);
}

static void test_string_value_is_owned(void) {
    char               text[] = "value";
    dcg_constant_node* node   = c_dcg_node_new_const_string(text, NULL);

    DCG_CHECK(c_dcg_var_as_string(&node->base.out) != text); /* a copy, not the caller's pointer */
    text[0] = 'X';
    DCG_CHECK_STR(c_dcg_var_as_string(&node->base.out), "value"); /* immune to the source */
    DCG_CHECK_STR(node->base.repr, "value");                      /* the repr has its own copy too */

    /* Replacing the value releases the copy it replaces. */
    DCG_CHECK_INT(c_dcg_node_const_set(node, dcg_t_var_string("second")), DCG_OK);
    DCG_CHECK_STR(c_dcg_var_as_string(&node->base.out), "second");

    c_dcg_node_free_const(node);
}

static void test_value_access(void) {
    dcg_constant_node* node = c_dcg_node_new_const_double(2.5, NULL);

    DCG_CHECK(c_dcg_node_const_get(node) == &node->base.out);
    DCG_CHECK(c_dcg_var_as_double(c_dcg_node_const_get(node)) == 2.5);

    DCG_CHECK_INT(c_dcg_node_const_set(node, dcg_t_var_int(7)), DCG_OK);
    DCG_CHECK_INT(node->base.out.dtype, VAR_TYPE_INT);
    DCG_CHECK_INT(c_dcg_var_as_int(c_dcg_node_const_get(node)), 7);

    DCG_CHECK(c_dcg_node_const_get(NULL) == NULL);
    DCG_CHECK_INT(c_dcg_node_const_set(NULL, dcg_t_var_int(1)), DCG_ERR_INVALID_ARG);

    c_dcg_node_free_const(node);
}

static void test_in_the_graph(void) {
    /* A constant is a node: it links into a graph like any other. */
    dcg_node* root = dcg_t_node_root("Entry Point");
    dcg_node* lhs  = dcg_t_node_double("lhs", 1.0);
    dcg_node* rhs  = dcg_t_node_double("rhs", 2.0);

    DCG_CHECK_INT(c_dcg_node_append(root, lhs, DCG_NO_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_append(lhs, rhs, DCG_TRUE_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_subtree_size(root), 3);
    DCG_CHECK_INT(c_dcg_node_depth(rhs), 2);

    dcg_t_trace_tree("tree(root -> lhs -> rhs)", root);
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 3); /* root, lhs, rhs */
}

static void test_variable_lifecycle(void) {
    /* A variable holds no value of its own: its out is a REFERENCE to the slot
     * it was built over, so reading it reads that slot, live. */
    dcg_var_t          slot;
    dcg_variable_node* var = NULL;

    (void) c_dcg_var_init_int(&slot, 42);
    var = c_dcg_node_new_var("state.n", "n", 1, &slot, NULL, NULL);
    DCG_CHECK(var != NULL);
    DCG_CHECK_INT(var->base.ntype, DCG_NODE_VARIABLE);
    DCG_CHECK(c_dcg_node_type_is_input(var->base.ntype));
    DCG_CHECK_STR(var->base.repr, "state.n");
    DCG_CHECK_STR(var->key, "n");
    DCG_CHECK(var->logic_group == NULL); /* built over a bare slot, not a store */
    DCG_CHECK_INT(c_dcg_var_as_int(&var->base.out), 42);
    dcg_t_trace_node("variable(state.n)", &var->base);

    /* The slot moves and the variable moves with it. */
    (void) c_dcg_var_init_int(&slot, 43);
    DCG_CHECK_INT(c_dcg_var_as_int(&var->base.out), 43);
    DCG_CHECK(c_dcg_var_as_ref(&var->base.out) == (const void*) &slot.value);

    c_dcg_node_free_var(var);
    c_dcg_node_free_var(NULL);

    /* A slot left out is not an error: the node is built reflecting nothing, and
     * c_dcg_node_var_bind() gives it one when the store it reads exists. A read
     * often has to be built before the thing it reads. */
    var = c_dcg_node_new_var("v", "v", 1, NULL, NULL, NULL);
    DCG_CHECK(var != NULL);
    DCG_CHECK(c_dcg_var_is_null(&var->base.out)); /* reflecting nothing yet */

    DCG_CHECK_INT(c_dcg_node_var_bind(var, &slot), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_as_int(&var->base.out), 43); /* live, from the bound slot */
    DCG_CHECK(c_dcg_var_as_ref(&var->base.out) == (const void*) &slot.value);

    /* Moving the slot moves the read, and rebinding points it somewhere else. */
    (void) c_dcg_var_init_int(&slot, 44);
    DCG_CHECK_INT(c_dcg_var_as_int(&var->base.out), 44);

    dcg_var_t other;
    (void) c_dcg_var_init_int(&other, 7);
    DCG_CHECK_INT(c_dcg_node_var_bind(var, &other), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_as_int(&var->base.out), 7);

    /* Nothing to bind to, or nothing to bind: refused. */
    DCG_CHECK_INT(c_dcg_node_var_bind(var, NULL), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_node_var_bind(NULL, &slot), DCG_ERR_INVALID_ARG);
    c_dcg_node_free_var(var);

    /* Everything else may be left out: the key and the group are optional. */
    var = c_dcg_node_new_var(NULL, NULL, 0, &slot, NULL, NULL);
    DCG_CHECK(var != NULL);
    DCG_CHECK(var->base.repr == NULL);
    DCG_CHECK(var->key == NULL);
    c_dcg_node_free_var(var);
}

static void test_variable_key_is_owned(void) {
    /* The key is the node's own copy, nested under it, and the copy takes the
     * length it was given - so a key that came out of a store, where a name is
     * a pointer and a length rather than a C string, needs no termination. */
    char               key[16] = "exposure";
    dcg_var_t          slot;
    dcg_variable_node* var = NULL;

    (void) c_dcg_var_init_int(&slot, 1);
    var = c_dcg_node_new_var("exposure", key, 8, &slot, NULL, NULL);
    DCG_CHECK(var != NULL);
    DCG_CHECK(var->key != key); /* a copy, not the caller's pointer */

    key[0] = 'X';
    DCG_CHECK_STR(var->key, "exposure"); /* immune to the source */

    c_dcg_node_free_var(var);

    /* A key that is a prefix of its buffer is copied to exactly its length. */
    (void) snprintf(key, sizeof(key), "sigma99");
    var = c_dcg_node_new_var("sigma", key, 5, &slot, NULL, NULL);
    DCG_CHECK(var != NULL);
    DCG_CHECK_STR(var->key, "sigma");
    DCG_CHECK_INT(strlen(var->key), 5);
    c_dcg_node_free_var(var);
}

static void test_variable_is_not_flat(void) {
    /* A variable carries a key and a group, so the block a literal lives in is
     * too small for it: only its own constructor knows how big the block is. */
    DCG_CHECK(!c_dcg_node_type_is_flat(DCG_NODE_VARIABLE));
    DCG_CHECK(c_dcg_node_new(DCG_NODE_VARIABLE, "v", NULL) == NULL);
    DCG_CHECK(c_dcg_node_new_const(DCG_NODE_VARIABLE, "v", NULL) == NULL);

    /* The family free routes a variable to its own teardown, so the dispatcher
     * releases it all - the key included - through the one entry point. */
    dcg_var_t          slot;
    dcg_variable_node* var = NULL;

    (void) c_dcg_var_init_int(&slot, 1);
    var = c_dcg_node_new_var("v", "key", 3, &slot, NULL, NULL);
    DCG_CHECK(var != NULL);
    c_dcg_node_free_generic(&var->base);
}

int main(void) {
    (void) printf("test_c_const\n");
    DCG_RUN(test_lifecycle);
    DCG_RUN(test_typed_construction);
    DCG_RUN(test_string_value_is_owned);
    DCG_RUN(test_value_access);
    DCG_RUN(test_in_the_graph);
    DCG_RUN(test_variable_lifecycle);
    DCG_RUN(test_variable_key_is_owned);
    DCG_RUN(test_variable_is_not_flat);
    DCG_SUMMARY("test_c_const");
    return dcg_test_failures == 0 ? 0 : 1;
}
