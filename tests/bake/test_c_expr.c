/*
 * c_expr.h - the expression family: the operator, the operand slots, and how an
 * operand reads the node it was bound to.
 */

#include <decision_graph/decision_tree/bake/c_expr.h>
#include <decision_graph/decision_tree/bake/c_const.h>
#include <decision_graph/decision_tree/bake/c_collection.h>

#include <decision_graph/decision_tree/bake/c_action.h>

#include <decision_graph/decision_tree/bake/c_hierarchy.h>

#include "test_util.h"

static void test_lifecycle(void) {
    /* The block is sized for its operands, so the count is fixed at birth. */
    dcg_expression_node* node = c_dcg_node_new_expr(3, DCG_NODE_TERNARY, NULL);
    DCG_CHECK(node != NULL);
    DCG_CHECK_INT(node->n_args, 3);
    DCG_CHECK_INT(node->base.ntype, DCG_NODE_TERNARY);
    DCG_CHECK_INT(node->op, DCG_OP_NONE);

    /* Every operand starts empty, not uninitialized. */
    for (size_t i = 0; i < 3; i++) {
        DCG_CHECK_INT(node->args[i].dtype, VAR_TYPE_RAW_PTR);
        DCG_CHECK(c_dcg_var_is_null(&node->args[i]));
    }

    /* The family struct IS the base node. */
    DCG_CHECK((dcg_node*) node == &node->base);
    c_dcg_node_free_expr(node);

    /* An operand count of zero falls back to the default, not to no operands. */
    node = c_dcg_node_new_expr(0, DCG_NODE_CALL, NULL);
    DCG_CHECK(node != NULL);
    DCG_CHECK_INT(node->n_args, DCG_EXPR_DEFAULT_ARGS);
    c_dcg_node_free_expr(node);

    /* A constant kind is not an expression. */
    DCG_CHECK(c_dcg_node_new_expr(2, DCG_NODE_DOUBLE, NULL) == NULL);

    /* Nor does the base constructor build one: the operand array does not fit
     * the base header, so the family constructor is the only way in. */
    DCG_CHECK(!c_dcg_node_type_is_flat(DCG_NODE_BINARY));
    DCG_CHECK(c_dcg_node_new(DCG_NODE_BINARY, "x", NULL) == NULL);
}

static void test_operator_and_repr(void) {
    dcg_node*            src  = dcg_t_node_root("Entry Point");
    dcg_expression_node* node = c_dcg_node_new_expr_binary(DCG_OP_ADD, src, src, NULL);

    DCG_CHECK(node != NULL);
    DCG_CHECK_INT(node->op, DCG_OP_ADD);
    DCG_CHECK_STR(node->base.repr, "Entry Point + Entry Point"); /* composed from the inputs */
    dcg_t_trace_expr("binary ADD", node);
    c_dcg_node_free_expr(node);

    node = c_dcg_node_new_expr_binary(DCG_OP_EQ, src, src, NULL);
    DCG_CHECK_STR(node->base.repr, "Entry Point == Entry Point");
    c_dcg_node_free_expr(node);

    node = c_dcg_node_new_expr_unary(DCG_OP_NOT, src, NULL);
    DCG_CHECK_INT(node->base.ntype, DCG_NODE_UNARY);
    DCG_CHECK_STR(node->base.repr, "~Entry Point"); /* one operand: the token in front */
    dcg_t_trace_expr("unary NOT", node);
    c_dcg_node_free_expr(node);

    /* A ternary with no operator is the plain if-expression. */
    node = c_dcg_node_new_expr_ternary(DCG_OP_NONE, src, src, src, NULL);
    DCG_CHECK_INT(node->base.ntype, DCG_NODE_TERNARY);
    DCG_CHECK_STR(node->base.repr, "Entry Point ? Entry Point : Entry Point");
    dcg_t_trace_expr("ternary NONE", node);
    c_dcg_node_free_expr(node);

    /* A call is written as the function it calls, with the operands as its
     * arguments: the repr the caller gives is the callee's name. */
    dcg_node* vars[2] = {src, src};
    node              = c_dcg_node_new_expr_call(DCG_OP_NONE, vars, 2, "my_call", NULL);
    DCG_CHECK_INT(node->base.ntype, DCG_NODE_CALL);
    DCG_CHECK_STR(node->base.repr, "my_call(Entry Point, Entry Point)");
    dcg_t_trace_expr("call my_call", node);
    c_dcg_node_free_expr(node);

    /* A constructor refuses to build over a missing input. */
    DCG_CHECK(c_dcg_node_new_expr_binary(DCG_OP_ADD, src, NULL, NULL) == NULL);
    DCG_CHECK(c_dcg_node_new_expr_unary(DCG_OP_NOT, NULL, NULL) == NULL);
    DCG_CHECK(c_dcg_node_new_expr_call(DCG_OP_NONE, NULL, 2, "c", NULL) == NULL);
    DCG_CHECK(c_dcg_node_new_expr_call(DCG_OP_NONE, vars, 0, "c", NULL) == NULL);

    c_dcg_node_free(src);
}

static void test_operator_codes(void) {
    /* The operator tables, which the expression family owns. */
    DCG_CHECK_STR(c_dcg_op_code_name(DCG_OP_NONE), "NONE");
    DCG_CHECK_STR(c_dcg_op_code_name(DCG_OP_ADD), "ADD");
    DCG_CHECK_STR(c_dcg_op_code_name(DCG_OP_FLOORDIV), "FLOORDIV");
    DCG_CHECK_STR(c_dcg_op_code_name(DCG_OP_GE), "GE");
    DCG_CHECK_STR(c_dcg_op_code_name(DCG_OP_GETITEM), "GETITEM");
    DCG_CHECK_STR(c_dcg_op_code_name((dcg_op_code) 0x0199), "UNKNOWN");

    /* The symbol answers for the arity it is asked about. */
    DCG_CHECK_STR(c_dcg_op_code_symbol(DCG_OP_NONE, 2), "");
    DCG_CHECK_STR(c_dcg_op_code_symbol(DCG_OP_SUB, 2), "-");
    DCG_CHECK_STR(c_dcg_op_code_symbol(DCG_OP_LE, 2), "<=");
    DCG_CHECK_STR(c_dcg_op_code_symbol(DCG_OP_AND, 2), "&");
    DCG_CHECK_STR(c_dcg_op_code_symbol(DCG_OP_GETITEM, 2), "[]");
    DCG_CHECK_STR(c_dcg_op_code_symbol(DCG_OP_NEG, 1), "-");
    DCG_CHECK_STR(c_dcg_op_code_symbol(DCG_OP_NOT, 1), "~");
    DCG_CHECK_STR(c_dcg_op_code_symbol(DCG_OP_ADD, 3), ""); /* the if-form writes none */
    DCG_CHECK_STR(c_dcg_op_code_symbol(DCG_OP_ADD, 0), "");

    (void) printf("    %-26s %s\n", "op table", "ADD + | SUB - | NEG - (1) | NOT ~ (1) | GETITEM [] | if-form (3) none");

    DCG_CHECK_INT(c_dcg_op_code_node_type(DCG_OP_NEG), DCG_NODE_UNARY);
    DCG_CHECK_INT(c_dcg_op_code_node_type(DCG_OP_NOT), DCG_NODE_UNARY);
    DCG_CHECK_INT(c_dcg_op_code_node_type(DCG_OP_MUL), DCG_NODE_BINARY);
    DCG_CHECK_INT(c_dcg_op_code_node_type(DCG_OP_GETITEM), DCG_NODE_BINARY);
    DCG_CHECK_INT(c_dcg_op_code_node_type(DCG_OP_NONE), DCG_NODE_OP);
}

static void test_repr_styles(void) {
    /* The two renderings the capi calls op-style and func-style, over the same
     * inputs - and the composed repr an operator node is built with. */
    dcg_node* twelve    = dcg_t_node_int("12", 12);
    dcg_node* three     = dcg_t_node_int("3", 3);
    dcg_node* inputs[2] = {twelve, three};
    char      buf[DCG_NODE_STRING_MAXLEN];

    DCG_CHECK_INT(c_dcg_node_expr_op_style(inputs, 2, DCG_OP_SUB, buf, sizeof(buf)), (int) strlen("12 - 3"));
    DCG_CHECK_STR(buf, "12 - 3");
    (void) printf("    %-26s 12 - 3\n", "op-style SUB");

    DCG_CHECK_INT(c_dcg_node_expr_func_style(inputs, 2, DCG_OP_SUB, NULL, buf, sizeof(buf)), (int) strlen("SUB(12, 3)"));
    DCG_CHECK_STR(buf, "SUB(12, 3)");
    (void) printf("    %-26s SUB(12, 3)\n", "func-style SUB");

    /* A name of the caller's is what a call writes. */
    DCG_CHECK_INT(c_dcg_node_expr_func_style(inputs, 2, DCG_OP_NONE, "atr", buf, sizeof(buf)), (int) strlen("atr(12, 3)"));
    DCG_CHECK_STR(buf, "atr(12, 3)");
    (void) printf("    %-26s atr(12, 3)\n", "func-style named");

    /* The same operator, one operand: the token in front of it. */
    DCG_CHECK_INT(c_dcg_node_expr_op_style(inputs, 1, DCG_OP_NEG, buf, sizeof(buf)), (int) strlen("-12"));
    DCG_CHECK_STR(buf, "-12");

    /* And the repr the node is built with is exactly that rendering. */
    dcg_expression_node* minus = c_dcg_node_new_expr_binary(DCG_OP_SUB, twelve, three, NULL);
    DCG_CHECK_STR(minus->base.repr, "12 - 3");
    dcg_t_trace_expr("binary SUB(12, 3)", minus);

    dcg_expression_node* neg = c_dcg_node_new_expr_unary(DCG_OP_NEG, twelve, NULL);
    DCG_CHECK_STR(neg->base.repr, "-12"); /* the worked example: symbol, then the input */
    dcg_t_trace_expr("unary NEG(12)", neg);

    /* A nested expression is parenthesized, so the text reads the way it
     * evaluates: the multiplication is one operand of the sum. */
    dcg_node* b = dcg_t_node_double("b", 2.0);
    dcg_node* c = dcg_t_node_double("c", 3.0);
    dcg_node* a = dcg_t_node_double("a", 1.0);

    dcg_expression_node* product = c_dcg_node_new_expr_binary(DCG_OP_MUL, b, c, NULL);
    DCG_CHECK_STR(product->base.repr, "b * c");

    dcg_expression_node* sum = c_dcg_node_new_expr_binary(DCG_OP_ADD, a, &product->base, NULL);
    DCG_CHECK_STR(sum->base.repr, "a + (b * c)");
    dcg_t_trace_expr("binary ADD(a, MUL(b, c))", sum);

    /* ... and one level deeper, the parentheses nest. */
    dcg_expression_node* negation = c_dcg_node_new_expr_unary(DCG_OP_NEG, &sum->base, NULL);
    DCG_CHECK_STR(negation->base.repr, "-(a + (b * c))");
    dcg_t_trace_expr("unary NEG(ADD(a, MUL(b, c)))", negation);

    c_dcg_node_free_expr(negation);
    c_dcg_node_free_expr(sum);
    c_dcg_node_free_expr(product);
    c_dcg_node_free(a);
    c_dcg_node_free(b);
    c_dcg_node_free(c);

    /* A NULL input, a zero capacity and a missing buffer are refused. */
    DCG_CHECK_INT(c_dcg_node_expr_op_style(NULL, 2, DCG_OP_SUB, buf, sizeof(buf)), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_node_expr_op_style(inputs, 0, DCG_OP_SUB, buf, sizeof(buf)), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_node_expr_func_style(inputs, 2, DCG_OP_SUB, NULL, buf, 0), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_node_expr_alias(NULL, buf, sizeof(buf)), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_node_expr_alias(twelve, buf, sizeof(buf)), 2);
    DCG_CHECK_STR(buf, "12");

    c_dcg_node_free_expr(minus);
    c_dcg_node_free_expr(neg);
    c_dcg_node_free(twelve);
    c_dcg_node_free(three);
}

static void test_bound_operands(void) {
    /* An operand bound to a node REFERS to that node's out slot: the expression
     * reads what the input holds when it is read, which is what makes a graph
     * evaluate - the value is produced later, by the node that owns it. */
    dcg_var_t          value;
    dcg_variable_node* var = NULL;
    dcg_expression_node* node = NULL;

    (void) c_dcg_var_init_double(&value, 2.5);
    var = c_dcg_node_new_var("x", &value, NULL);
    DCG_CHECK(var != NULL);

    node = c_dcg_node_new_expr_binary(DCG_OP_ADD, &var->base, &var->base, NULL);
    DCG_CHECK(node != NULL);

    /* A variable node holds no value of its own: its out is already a reference
     * to `value`, and the operand takes that reference as it is. Wrapping it in
     * another one would leave every read a hop short of the value it wants. */
    DCG_CHECK_INT(node->args[0].dtype, VAR_TYPE_DOUBLE_REF);
    DCG_CHECK(c_dcg_var_as_ref(&node->args[0]) == c_dcg_var_as_ref(&var->base.out));
    DCG_CHECK(c_dcg_var_as_double(&node->args[0]) == 2.5);
    DCG_CHECK_INT(c_dcg_var_as_int(&node->args[1]), 2);

    (void) c_dcg_var_init_double(&value, 4.0); /* read time, not bake time */
    DCG_CHECK(c_dcg_var_as_double(&node->args[0]) == 4.0);
    dcg_t_trace_expr("binary ADD(x, x)", node);

    c_dcg_node_free_expr(node);
    c_dcg_node_free_var(var);
}

static void test_folded_constants(void) {
    /* A constant is known when the graph is baked, so its value is folded in
     * rather than referred to: a literal needs no indirection to be read. */
    dcg_constant_node*   five = c_dcg_node_new_const_int(5, NULL);
    dcg_expression_node* node = c_dcg_node_new_expr_binary(DCG_OP_MUL, &five->base, &five->base, NULL);

    DCG_CHECK_INT(node->args[0].dtype, VAR_TYPE_INT); /* the value, not a reference */
    DCG_CHECK_INT(c_dcg_var_as_int(&node->args[0]), 5);
    dcg_t_trace_expr("binary MUL(five, five)", node);
    c_dcg_node_free_expr(node);
    c_dcg_node_free_const(five);

    /* A folded string is a copy nested under the expression, so the expression
     * is self-contained: its source can go away. */
    dcg_constant_node* text = c_dcg_node_new_const_string("abc", NULL);
    node                    = c_dcg_node_new_expr_unary(DCG_OP_NOT, &text->base, NULL);

    DCG_CHECK_INT(node->args[0].dtype, VAR_TYPE_STRING);
    DCG_CHECK_STR(c_dcg_var_as_string(&node->args[0]), "abc");
    DCG_CHECK(c_dcg_var_as_string(&node->args[0]) != text->base.out.value.as_string); /* a copy */

    dcg_t_trace_expr("unary NOT(text)", node);
    c_dcg_node_free_const(text);
    DCG_CHECK_STR(c_dcg_var_as_string(&node->args[0]), "abc"); /* the expression kept its own */
    c_dcg_node_free_expr(node);                                /* LSan checks the copy went with it */
}

static void test_binding(void) {
    dcg_node*            input = dcg_t_node_root("Entry Point");
    dcg_expression_node* node  = c_dcg_node_new_expr(2, DCG_NODE_BINARY, NULL);

    DCG_CHECK_INT(c_dcg_node_expr_bind(node, 0, input), DCG_OK);
    DCG_CHECK_INT(node->args[0].dtype, VAR_TYPE_BOOL_REF);
    DCG_CHECK(c_dcg_var_as_bool(&node->args[0])); /* a root's out is true */

    /* Rebinding a slot reads the new input instead. */
    dcg_constant_node* seven = c_dcg_node_new_const_int(7, NULL);
    DCG_CHECK_INT(c_dcg_node_expr_bind(node, 0, &seven->base), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_as_int(&node->args[0]), 7);
    c_dcg_node_free_const(seven);

    /* Out-of-range and NULL arguments are refused. */
    DCG_CHECK_INT(c_dcg_node_expr_bind(node, 2, input), DCG_ERR_RANGE);
    DCG_CHECK_INT(c_dcg_node_expr_bind(node, 0, NULL), DCG_ERR_INVALID_ARG);
    DCG_CHECK_INT(c_dcg_node_expr_bind(NULL, 0, input), DCG_ERR_INVALID_ARG);

    c_dcg_node_free_expr(node);
    c_dcg_node_free(input);
}

static void test_eval_style_loop(void) {
    /* What an evaluator does: read the operator, then loop over the operands.
     * No graph traversal, no child lookup - a flat array, each slot a reference
     * into the node it was bound to. */
    dcg_var_t            a;
    dcg_var_t            b;
    dcg_variable_node*   lhs = NULL;
    dcg_variable_node*   rhs = NULL;
    dcg_expression_node* node = NULL;

    (void) c_dcg_var_init_double(&a, 1.25);
    (void) c_dcg_var_init_int(&b, 2);
    lhs = c_dcg_node_new_var("a", &a, NULL);
    rhs = c_dcg_node_new_var("b", &b, NULL);

    node = c_dcg_node_new_expr_binary(DCG_OP_ADD, &lhs->base, &rhs->base, NULL);
    DCG_CHECK(node != NULL);

    double sum = 0.0;
    for (size_t i = 0; i < node->n_args; i++) sum += c_dcg_var_as_double(&node->args[i]);
    DCG_CHECK(sum == 3.25);

    (void) c_dcg_var_init_double(&a, 2.0); /* the operands follow their inputs */
    sum = 0.0;
    for (size_t i = 0; i < node->n_args; i++) sum += c_dcg_var_as_double(&node->args[i]);
    DCG_CHECK(sum == 4.0);

    dcg_t_trace_expr("binary ADD(a, b)", node);

    /* An expression is a node too: it links into a graph. */
    dcg_node* root = dcg_t_node_root("Entry Point");
    DCG_CHECK_INT(c_dcg_node_append(root, &node->base, DCG_NO_CONDITION), DCG_OK);
    DCG_CHECK_INT(c_dcg_node_child_count(root), 1);
    DCG_CHECK(c_dcg_node_first_child(root) == &node->base);
    DCG_CHECK_INT(c_dcg_node_teardown_root(root), 2); /* the root and the expression under it */
    c_dcg_node_free_var(lhs);
    c_dcg_node_free_var(rhs);
}

int main(void) {
    (void) printf("test_c_expr\n");
    DCG_RUN(test_lifecycle);
    DCG_RUN(test_operator_and_repr);
    DCG_RUN(test_operator_codes);
    DCG_RUN(test_repr_styles);
    DCG_RUN(test_bound_operands);
    DCG_RUN(test_folded_constants);
    DCG_RUN(test_binding);
    DCG_RUN(test_eval_style_loop);
    DCG_SUMMARY("test_c_expr");
    return dcg_test_failures == 0 ? 0 : 1;
}
