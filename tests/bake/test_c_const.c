/*
 * c_const.h - the constant family: lifecycle, typed construction, the value it
 * stands for, and what its repr defaults to.
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
    DCG_CHECK(c_dcg_node_type_is_flat(DCG_NODE_CONST));
    DCG_CHECK(c_dcg_node_type_is_flat(DCG_NODE_DOUBLE));
    node = (dcg_constant_node*) c_dcg_node_new(DCG_NODE_CONST, "empty", NULL);
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
    DCG_CHECK_INT(node->base.ntype, DCG_NODE_CONST);
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

int main(void) {
    (void) printf("test_c_const\n");
    DCG_RUN(test_lifecycle);
    DCG_RUN(test_typed_construction);
    DCG_RUN(test_string_value_is_owned);
    DCG_RUN(test_value_access);
    DCG_RUN(test_in_the_graph);
    DCG_SUMMARY("test_c_const");
    return dcg_test_failures == 0 ? 0 : 1;
}
