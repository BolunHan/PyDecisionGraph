/*
 * c_expr.h - the expression family: the operator, the operand slots, and how an
 * operand reads the node it was bound to.
 */

#include <decision_graph/decision_tree/bake/c_expr.h>
#include <decision_graph/decision_tree/bake/c_const.h>
#include <decision_graph/decision_tree/bake/c_collections.h>

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
    var = c_dcg_node_new_var("x", "x", 1, &value, NULL, NULL);
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

static void test_operand_references(void) {
    /* An operand is HELD, whatever the slot took: binding takes one reference on
     * it and teardown gives it back - the same rule for a literal, a read and an
     * expression, so there is no case that has to be reasoned about twice. The
     * block is shared while the expression is alive, which is what makes freeing
     * an operand by hand a loud mistake instead of a slot left reading freed
     * memory - and what keeps the node readable after one of its holders lets go.
     */
    dcg_constant_node*   five  = c_dcg_node_new_const_int(5, NULL);
    dcg_expression_node* inner = c_dcg_node_new_expr_unary(DCG_OP_NEG, &five->base, NULL);
    DCG_CHECK(inner != NULL);
    DCG_CHECK(inner->components[0] == &five->base); /* a folded literal is held all the same */
    c_ap_decref(&five->base);                /* this caller's own reference goes back */

    allocator_protocol* protocol = c_ap_protocol_from_ptr(inner);
    int64_t             owned    = atomic_load_explicit(&protocol->ref_count, memory_order_acquire);

    dcg_expression_node* outer = c_dcg_node_new_expr_unary(DCG_OP_NOT, &inner->base, NULL);
    DCG_CHECK(outer != NULL);
    DCG_CHECK(outer->components[0] == &inner->base); /* held, not just pointed at */
    DCG_CHECK(atomic_load_explicit(&protocol->ref_count, memory_order_acquire) == owned + 1);

    /* One holder letting go gives the reference back; the node is untouched. */
    c_dcg_node_free_expr(outer);
    DCG_CHECK(atomic_load_explicit(&protocol->ref_count, memory_order_acquire) == owned);
    DCG_CHECK_INT(inner->op, DCG_OP_NEG);
    DCG_CHECK_INT(inner->n_args, 1);

    /* Rebinding a slot to the operand it already holds does not pile up
     * references, so the hold can be moved without leaking one. */
    outer = c_dcg_node_new_expr_unary(DCG_OP_NOT, &inner->base, NULL);
    DCG_CHECK_INT(c_dcg_node_expr_bind(outer, 0, &inner->base), DCG_OK);
    DCG_CHECK(atomic_load_explicit(&protocol->ref_count, memory_order_acquire) == owned + 1);
    c_dcg_node_free_expr(outer);
    DCG_CHECK(atomic_load_explicit(&protocol->ref_count, memory_order_acquire) == owned);

    /* Rebinding to a DIFFERENT operand gives the old one back, so a slot never
     * holds two operands' references at once. */
    dcg_constant_node*   seven = c_dcg_node_new_const_int(7, NULL);
    dcg_expression_node* again = c_dcg_node_new_expr_unary(DCG_OP_NOT, &inner->base, NULL);
    DCG_CHECK(again != NULL);
    DCG_CHECK(atomic_load_explicit(&protocol->ref_count, memory_order_acquire) == owned + 1);

    DCG_CHECK_INT(c_dcg_node_expr_bind(again, 0, &seven->base), DCG_OK);
    DCG_CHECK(atomic_load_explicit(&protocol->ref_count, memory_order_acquire) == owned); /* inner's hold went back */
    DCG_CHECK(again->components[0] == &seven->base);
    c_ap_decref(&seven->base);
    c_dcg_node_free_expr(again);

    /* The literal a bound slot took the value of is held as well, so the slot
     * reads what it folded in for as long as the expression lives. */
    seven                       = c_dcg_node_new_const_int(7, NULL);
    dcg_expression_node* plain  = c_dcg_node_new_expr_unary(DCG_OP_NOT, &seven->base, NULL);
    DCG_CHECK(plain != NULL);
    DCG_CHECK(plain->components[0] == &seven->base);
    c_ap_decref(&seven->base); /* the expression's hold is what keeps it readable */
    DCG_CHECK_INT(c_dcg_var_as_int(&plain->args[0]), 7);

    c_dcg_node_free_expr(plain);
    c_dcg_node_free_expr(inner); /* its own hold on five is given back here, and the block dies */
}

static void test_folded_constants(void) {
    /* A constant is known when the graph is baked, so its value is folded in
     * rather than referred to: a literal needs no indirection to be read. */
    dcg_constant_node*   five = c_dcg_node_new_const_int(5, NULL);
    dcg_expression_node* node = c_dcg_node_new_expr_binary(DCG_OP_MUL, &five->base, &five->base, NULL);

    DCG_CHECK_INT(node->args[0].dtype, VAR_TYPE_INT); /* the value, not a reference */
    DCG_CHECK_INT(c_dcg_var_as_int(&node->args[0]), 5);
    dcg_t_trace_expr("binary MUL(five, five)", node);
    c_dcg_node_free_expr(node); /* gives back its two holds on `five` */
    c_dcg_node_free_const(five);

    /* A folded string is a copy nested under the expression, so the expression
     * is self-contained: its source can go away. */
    dcg_constant_node* text = c_dcg_node_new_const_string("abc", NULL);
    node                    = c_dcg_node_new_expr_unary(DCG_OP_NOT, &text->base, NULL);

    DCG_CHECK_INT(node->args[0].dtype, VAR_TYPE_STRING);
    DCG_CHECK_STR(c_dcg_var_as_string(&node->args[0]), "abc");
    DCG_CHECK(c_dcg_var_as_string(&node->args[0]) != text->base.out.value.as_string); /* a copy */

    dcg_t_trace_expr("unary NOT(text)", node);
    c_ap_decref(&text->base); /* folded in, and held: this reference goes back */
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
    c_ap_decref(&seven->base); /* bound into the slot: this reference goes back */

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
    lhs = c_dcg_node_new_var("a", "a", 1, &a, NULL, NULL);
    rhs = c_dcg_node_new_var("b", "b", 1, &b, NULL, NULL);

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

static void test_every_op_keeps_its_own_rule(void) {
    /* Which rule evaluates a node is answered ONCE, out of the flat list of the
     * family's rules (DCG_EXPR_EVAL_FNS) - and where the answer is KEPT is what
     * DCG_EVAL_DIRECT_HOOKS decides: injected onto the node as its type-eval rule
     * as it is built, or found again at evaluation time by the family's own entry
     * (c_dcg_node_expr_eval). Both are asserted here, in every operator, because a
     * rule that is right for one operator and wrong for another is exactly the
     * drift this design exists to prevent.
     *
     * The rules drive their OWN operands, so nothing has to be filled in before
     * one is called: the workspace is the node's own field, over the node's own
     * components. */
    static const struct {
        dcg_op_code      op;
        dcg_node_type    ntype;
        dcg_node_hook_fn rule;
    } cases[] = {
        {DCG_OP_NEG, DCG_NODE_UNARY, c_dcg_node_expr_eval_neg},
        {DCG_OP_NOT, DCG_NODE_UNARY, c_dcg_node_expr_eval_not},
        {DCG_OP_ADD, DCG_NODE_BINARY, c_dcg_node_expr_eval_add},
        {DCG_OP_SUB, DCG_NODE_BINARY, c_dcg_node_expr_eval_sub},
        {DCG_OP_MUL, DCG_NODE_BINARY, c_dcg_node_expr_eval_mul},
        {DCG_OP_DIV, DCG_NODE_BINARY, c_dcg_node_expr_eval_div},
        {DCG_OP_FLOORDIV, DCG_NODE_BINARY, c_dcg_node_expr_eval_floordiv},
        {DCG_OP_POW, DCG_NODE_BINARY, c_dcg_node_expr_eval_pow},
        {DCG_OP_EQ, DCG_NODE_BINARY, c_dcg_node_expr_eval_eq},
        {DCG_OP_NE, DCG_NODE_BINARY, c_dcg_node_expr_eval_ne},
        {DCG_OP_GT, DCG_NODE_BINARY, c_dcg_node_expr_eval_gt},
        {DCG_OP_GE, DCG_NODE_BINARY, c_dcg_node_expr_eval_ge},
        {DCG_OP_LT, DCG_NODE_BINARY, c_dcg_node_expr_eval_lt},
        {DCG_OP_LE, DCG_NODE_BINARY, c_dcg_node_expr_eval_le},
        {DCG_OP_AND, DCG_NODE_BINARY, c_dcg_node_expr_eval_and},
        {DCG_OP_OR, DCG_NODE_BINARY, c_dcg_node_expr_eval_or}
    };

    dcg_constant_node* lhs = c_dcg_node_new_const_int(6, NULL);
    dcg_constant_node* rhs = c_dcg_node_new_const_int(7, NULL);
    dcg_node*          vars[1] = {&lhs->base};

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        dcg_op_code op = cases[i].op;

        /* The flat list holds the operator's own rule, at the slot its code
         * indexes - the code flattened into its family and its variant. */
        DCG_CHECK(DCG_EXPR_EVAL_FNS[c_dcg_op_code_index(op)] == cases[i].rule);

        dcg_expression_node* node = cases[i].ntype == DCG_NODE_UNARY ? c_dcg_node_new_expr_unary(op, &lhs->base, NULL) : c_dcg_node_new_expr_binary(op, &lhs->base, &rhs->base, NULL);
        DCG_CHECK(node != NULL);

#if DCG_EVAL_DIRECT_HOOKS
        /* The injection: the node carries ITS operator's rule, from the moment its
         * operator is set. */
        DCG_CHECK(node->base.eval_ctx.type_eval_fn == cases[i].rule);
#else
        /* No injection in this build, so nothing is on the node and the rule is
         * found from the code when the node is evaluated. */
        DCG_CHECK(node->base.eval_ctx.type_eval_fn == NULL);
#endif

        /* Either way the same rule runs - and it is the node's whole evaluation:
         * it runs its own components, fills its own workspace, and applies its
         * own kernel, with no dispatch and nothing to fill in beforehand. */
        DCG_CHECK_INT(c_dcg_node_expr_eval(&node->base, NULL), DCG_OK);
        DCG_CHECK(!c_dcg_var_is_null(&node->base.out));
        c_dcg_node_free_expr(node);
    }
    (void) printf("    %-26s %zu operators, each with its own rule and its own value\n", "flat rule list", sizeof(cases) / sizeof(cases[0]));

    /* An operator the node's arity has no rule for refuses - in either build. */
    dcg_expression_node* unary_add = c_dcg_node_new_expr_unary(DCG_OP_ADD, &lhs->base, NULL);
    DCG_CHECK(unary_add != NULL);
#if DCG_EVAL_DIRECT_HOOKS
    DCG_CHECK(unary_add->base.eval_ctx.type_eval_fn == c_dcg_node_expr_eval_refuse);
#endif
    DCG_CHECK_INT(c_dcg_node_expr_eval(&unary_add->base, NULL), DCG_ERR_TYPE);
    c_dcg_node_free_expr(unary_add);

    dcg_expression_node* binary_neg = c_dcg_node_new_expr_binary(DCG_OP_NEG, &lhs->base, &rhs->base, NULL);
    DCG_CHECK(binary_neg != NULL);
    DCG_CHECK_INT(c_dcg_node_expr_eval(&binary_neg->base, NULL), DCG_ERR_TYPE);
    c_dcg_node_free_expr(binary_neg);

    /* The slots that name no operator of a family this one has - a family head,
     * the access operators - refuse; one that names no operator at all reports
     * the node it was handed. */
    DCG_CHECK(DCG_EXPR_EVAL_FNS[c_dcg_op_code_index(DCG_OP_GETITEM)] == c_dcg_node_expr_eval_refuse);
    DCG_CHECK(DCG_EXPR_EVAL_FNS[c_dcg_op_code_index(DCG_OP_NONE)] == c_dcg_node_expr_eval_unknown);
    DCG_CHECK(DCG_EXPR_EVAL_FNS[c_dcg_op_code_index(DCG_OP_ARITH)] == c_dcg_node_expr_eval_unknown);
    DCG_CHECK(c_dcg_node_expr_apply_fn_of(DCG_NODE_UNARY, DCG_OP_ADD) == c_dcg_node_expr_apply_refuse);
    DCG_CHECK(c_dcg_node_expr_apply_fn_of(DCG_NODE_BINARY, DCG_OP_NEG) == c_dcg_node_expr_apply_refuse);
    DCG_CHECK(c_dcg_node_expr_apply_fn_of(DCG_NODE_BINARY, DCG_OP_GETITEM) == c_dcg_node_expr_apply_refuse);

    /* An if-expression reads the arm its condition picks, and ONLY that arm: a
     * call on the side not taken - which has no evaluation at all - is not this
     * node's problem. */
    dcg_expression_node* ternary = c_dcg_node_new_expr_ternary(DCG_OP_NONE, &lhs->base, &rhs->base, &lhs->base, NULL);
    DCG_CHECK(ternary != NULL);
#if DCG_EVAL_DIRECT_HOOKS
    DCG_CHECK(ternary->base.eval_ctx.type_eval_fn == c_dcg_node_expr_eval_ternary);
#endif
    DCG_CHECK_INT(c_dcg_node_expr_eval(&ternary->base, NULL), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_as_int(&ternary->base.out), 7); /* 6 is true, so the then-arm */
    c_dcg_node_free_expr(ternary);

    dcg_expression_node* untaken = c_dcg_node_new_expr_call(DCG_OP_NONE, vars, 1, "never", NULL);
    dcg_expression_node* lazy    = c_dcg_node_new_expr_ternary(DCG_OP_NONE, &lhs->base, &rhs->base, &untaken->base, NULL);
    DCG_CHECK(untaken != NULL && lazy != NULL);
    DCG_CHECK_INT(c_dcg_node_expr_eval(&lazy->base, NULL), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_as_int(&lazy->base.out), 7); /* the else-arm holds the call, and is not reached */
    c_dcg_node_free_expr(lazy);
    c_dcg_node_free_expr(untaken);

    /* A call has no evaluation: saying so is its rule, in either build. */
    dcg_expression_node* call = c_dcg_node_new_expr_call(DCG_OP_NONE, vars, 1, "my_call", NULL);
    DCG_CHECK(call != NULL);
#if DCG_EVAL_DIRECT_HOOKS
    DCG_CHECK(call->base.eval_ctx.type_eval_fn == c_dcg_node_expr_eval_call);
#endif
    DCG_CHECK_INT(c_dcg_node_expr_eval(&call->base, NULL), DCG_ERR_TYPE);
    c_dcg_node_free_expr(call);

    /* A node born with no operator at all refuses until an operator arrives - and
     * setting one moves the rule with the code, which is what keeps the two from
     * ever disagreeing. */
    dcg_expression_node* bare = c_dcg_node_new_expr(2, DCG_NODE_BINARY, NULL);
    DCG_CHECK(bare != NULL);
#if DCG_EVAL_DIRECT_HOOKS
    DCG_CHECK(bare->base.eval_ctx.type_eval_fn == c_dcg_node_expr_eval_refuse);
#endif
    DCG_CHECK_INT(c_dcg_node_expr_eval(&bare->base, NULL), DCG_ERR_TYPE); /* no operator, no operands */
    DCG_CHECK_INT(c_dcg_node_expr_set_op(bare, DCG_OP_SUB), DCG_OK);
    DCG_CHECK_INT(bare->op, DCG_OP_SUB);
#if DCG_EVAL_DIRECT_HOOKS
    DCG_CHECK(bare->base.eval_ctx.type_eval_fn == c_dcg_node_expr_eval_sub);
#endif
    c_dcg_node_free_expr(bare);

    /* And the runtime dispatchers, which serve a caller with a code and no node,
     * reach the kernels the rules call - the same answer by the two ways in. */
    dcg_expression_node* node = c_dcg_node_new_expr_binary(DCG_OP_SUB, &lhs->base, &rhs->base, NULL);
    dcg_var_t            a;
    dcg_var_t            b;
    dcg_var_t            through_the_code;
    (void) c_dcg_var_init_int(&a, 9);
    (void) c_dcg_var_init_int(&b, 4);

    DCG_CHECK_INT(c_dcg_node_expr_apply_binary(&through_the_code, DCG_OP_SUB, &a, &b), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_as_int(&through_the_code), 5);
    DCG_CHECK_INT(c_dcg_node_expr_eval(&node->base, NULL), DCG_OK);
    DCG_CHECK_INT(c_dcg_var_as_int(&node->base.out), -1); /* 6 - 7, by the family's own entry */
    c_dcg_node_free_expr(node);

    /* An operator no arity has a kernel for: refused, with the code the
     * dispatchers have always reported for it. */
    DCG_CHECK_INT(c_dcg_node_expr_apply_unary(&through_the_code, DCG_OP_ADD, &a), DCG_ERR_TYPE);
    DCG_CHECK_INT(c_dcg_node_expr_apply_binary(&through_the_code, DCG_OP_NEG, &a, &b), DCG_ERR_TYPE);

    c_ap_decref(&lhs->base);
    c_ap_decref(&rhs->base);
}

int main(void) {
    (void) printf("test_c_expr\n");
    DCG_RUN(test_lifecycle);
    DCG_RUN(test_operator_and_repr);
    DCG_RUN(test_operator_codes);
    DCG_RUN(test_repr_styles);
    DCG_RUN(test_bound_operands);
    DCG_RUN(test_operand_references);
    DCG_RUN(test_folded_constants);
    DCG_RUN(test_binding);
    DCG_RUN(test_eval_style_loop);
    DCG_RUN(test_every_op_keeps_its_own_rule);
    DCG_SUMMARY("test_c_expr");
    return dcg_test_failures == 0 ? 0 : 1;
}
