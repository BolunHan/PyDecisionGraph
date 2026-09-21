#ifndef C_DCG_BAKE_EXPR_H
#define C_DCG_BAKE_EXPR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cbase/allocator_protocol/c_allocator_protocol.h>

#include <decision_graph/decision_tree/bake/c_node.h>

/*
 * The operator family: a node that computes a value from the values of other
 * nodes. The operands are the interesting part - they are not child edges, they
 * are VALUE SLOTS inside the expression, so evaluating one is a loop over a flat
 * array rather than a walk of the graph:
 *
 *   - an operand bound to another node is a REFERENCE to that node's out slot,
 *     read when the expression is evaluated (see c_dcg_var_init_ref), so an
 *     operand always sees what its input holds at that moment;
 *   - an operand bound to a constant is the value itself, folded in at bake
 *     time, because a literal needs no indirection to be read.
 *
 * Strings are the one thing a folded operand owns: the copy is nested under the
 * expression, so freeing the expression releases it. References own nothing.
 */

// ========== Constants ==========

/** Operand slots a call node is allocated with when the count is not given. */
#ifndef DCG_EXPR_DEFAULT_ARGS
#define DCG_EXPR_DEFAULT_ARGS 2U
#endif

/**
 * @brief Operator payload of an operator node.
 *
 * The family nibble mirrors the capi operator classes one-to-one
 * (arithmetic / comparison / logical / access), so a Python operator maps
 * straight onto an op code at bake time. Comparison and logical values are
 * deliberately identical to the capi int_enum values, which is what lets
 * the Cython layer cast instead of translate.
 */
typedef enum dcg_op_code {
    DCG_OP_NONE     = 0x0000,  // No operator (non-operator nodes).
    // === Arithmetic (capi MathExpressionOperator) ===
    DCG_OP_ARITH    = 0x0100,  // <- Mask and Generics
    DCG_OP_ADD      = 0x0101,  // a + b
    DCG_OP_SUB      = 0x0102,  // a - b
    DCG_OP_MUL      = 0x0103,  // a * b
    DCG_OP_DIV      = 0x0104,  // a / b
    DCG_OP_FLOORDIV = 0x0105,  // a // b
    DCG_OP_POW      = 0x0106,  // a ** b
    DCG_OP_NEG      = 0x0107,  // -a (unary)
    // === Comparison (capi ComparisonExpressionOperator int_enum) ===
    DCG_OP_COMPARE  = 0x0200,  // <- Mask and Generics
    DCG_OP_EQ       = 0x0201,  // a == b
    DCG_OP_NE       = 0x0202,  // a != b
    DCG_OP_GT       = 0x0203,  // a >  b
    DCG_OP_GE       = 0x0204,  // a >= b
    DCG_OP_LT       = 0x0205,  // a <  b
    DCG_OP_LE       = 0x0206,  // a <= b
    // === Logical (capi LogicalExpressionOperator int_enum) ===
    DCG_OP_LOGIC    = 0x0300,  // <- Mask and Generics
    DCG_OP_AND      = 0x0301,  // a and b
    DCG_OP_OR       = 0x0302,  // a or b
    DCG_OP_NOT      = 0x0303,  // not a (unary)
    // === Access (capi Attr / Getter expressions) ===
    DCG_OP_ACCESS   = 0x0400,  // <- Mask and Generics
    DCG_OP_ATTR     = 0x0401,  // a.attr
    DCG_OP_GETITEM  = 0x0402   // a[key]
} dcg_op_code;

/**
 * @brief Bit masks over an operator code: family nibble and variant number.
 */
typedef enum dcg_op_mask {
    DCG_OP_FAMILY_MASK  = 0x0F00,  // Extracts the family nibble of a dcg_op_code.
    DCG_OP_VARIANT_MASK = 0x00FF   // Extracts the variant number of a dcg_op_code.
} dcg_op_mask;

// ========== Structs ==========

/**
 * @brief An expression node: the base node plus its operator and its operands.
 *
 * The operands are VALUES, not child nodes - they sit inline in the block,
 * which is what makes evaluating an expression a loop over a flat array rather
 * than a walk of the graph. A graph keeps its edges for control flow; an
 * expression keeps its operands here.
 *
 * `args` is a flexible array member, so the block is allocated with exactly as
 * many operands as it needs and the node is never an embedded field. Each slot
 * is filled by binding an input node (c_dcg_node_expr_bind): a reference to the
 * input's out slot, or the folded value of a constant input. A string value
 * folded in is a copy nested under the node; a reference owns nothing.
 *
 * `components` is the node's hold on the operands it has to keep. A slot that
 * took an operand's VALUE - a constant, folded - is self-contained and holds
 * nothing, which is what lets a caller release a literal the moment it is bound.
 * A slot that refers to the operand's storage is not: released, the operand
 * would leave the slot reading a block that is gone. Binding such an operand
 * takes a reference on it (c_ap_incref) and teardown gives it back
 * (c_ap_decref), so the expression keeps working after its caller lets go. The
 * array is n_args entries - exactly as many as there are slots, which is why
 * there is no capacity field - and it sits in the node's OWN block, right behind
 * the operand slots, so the node has no separate block to release and the array
 * goes where the node goes.
 *
 * That is the C half of the hold. The Python wrapper keeps the other half, on
 * the wrapper objects rather than on the blocks, and both are needed: this one
 * keeps the BLOCK alive, the wrapper's keeps the WRAPPER alive.
 *
 * The repr is composed when the node is built, from the operator and the texts of
 * the nodes it was given: "-12" for a negation of 12, "12 - 3" for a difference,
 * "my_call(a, b)" for a call. Both renderings stay available afterwards -
 * c_dcg_node_expr_op_style() and c_dcg_node_expr_func_style() - which is what the
 * capi calls its op-style and func-style repr.
 *
 * The base node must stay the FIRST member: a dcg_expression_node* is therefore
 * a valid dcg_node*.
 */
typedef struct dcg_expression_node {
    dcg_node    base;        // The common node header. Must stay first.
    dcg_op_code op;          // The operator applied to the operands.
    size_t      n_args;      // Number of operands in `args`, and in `components`.
    dcg_node**  components;  // Operand pointers, n_args of them (see below).
    dcg_var_t   args[];      // Operand slots, n_args of them, then the pointers.
} dcg_expression_node;

/*
 * Where `components` points: the operand pointers are the second of the two
 * arrays the block is sized for, immediately behind the slots. Two arrays and
 * one allocation, because a flexible array member has to be last - and because
 * an array of pointers needs no more alignment than a var slot provides, the
 * slots being sizeof(dcg_var_t) apart.
 */
static inline dcg_node** c_dcg_node_expr_components(dcg_expression_node* node) {
    return (dcg_node**) (node->args + node->n_args);
}

// ========== Forward Declarations ==========

// Lifecycle
static inline dcg_expression_node* c_dcg_node_new_expr(size_t n_args, dcg_node_type ntype, allocator_protocol* allocator);
static inline void                 c_dcg_node_free_expr(dcg_expression_node* node);

// Typed constructors (the inputs are bound as operands, in order)
static inline dcg_expression_node* c_dcg_node_new_expr_unary(dcg_op_code op, dcg_node* src, allocator_protocol* allocator);
static inline dcg_expression_node* c_dcg_node_new_expr_binary(dcg_op_code op, dcg_node* var_0, dcg_node* var_1, allocator_protocol* allocator);
static inline dcg_expression_node* c_dcg_node_new_expr_ternary(dcg_op_code op, dcg_node* var_0, dcg_node* var_1, dcg_node* var_2, allocator_protocol* allocator);
static inline dcg_expression_node* c_dcg_node_new_expr_call(dcg_op_code op, dcg_node** vars, size_t n_vars, const char* repr, allocator_protocol* allocator);

// Operands
static inline int                  c_dcg_node_expr_bind(dcg_expression_node* node, size_t index, dcg_node* input);

// Composing the repr
static inline int                  c_dcg_node_expr_alias(const dcg_node* input, char* out, size_t cap);
static inline int                  c_dcg_node_expr_op_style(dcg_node* const* inputs, size_t n_inputs, dcg_op_code op, char* out, size_t cap);
static inline int                  c_dcg_node_expr_func_style(dcg_node* const* inputs, size_t n_inputs, dcg_op_code op, const char* name, char* out, size_t cap);

// ========== Operator Utilities ==========

/*
 * Operator names, one table per family. The variant numbers start at 1, so the
 * table index is the variant minus one.
 */
static const char* const           DCG_OP_ARITH_NAMES[]   = {"ADD", "SUB", "MUL", "DIV", "FLOORDIV", "POW", "NEG"};
static const char* const           DCG_OP_COMPARE_NAMES[] = {"EQ", "NE", "GT", "GE", "LT", "LE"};
static const char* const           DCG_OP_LOGIC_NAMES[]   = {"AND", "OR", "NOT"};
static const char* const           DCG_OP_ACCESS_NAMES[]  = {"ATTR", "GETITEM"};

/*
 * Operator symbols, one table per family, mirroring the capi's op_repr - the
 * form a node's repr is built from.
 */
static const char* const           DCG_OP_ARITH_SYMBOLS[]   = {"+", "-", "*", "/", "//", "**", "-"};
static const char* const           DCG_OP_COMPARE_SYMBOLS[] = {"==", "!=", ">", ">=", "<", "<="};
static const char* const           DCG_OP_LOGIC_SYMBOLS[]   = {"&", "|", "~"};
static const char* const           DCG_OP_ACCESS_SYMBOLS[]  = {".", "[]"};

/**
 * @brief The symbol of an operator ("+", "==", ...) as an arity writes it.
 *
 * One operand writes it in front (-12), two write it between (12 - 3), and three
 * are the if-form, which writes no symbol at all. Which of the two a caller gets
 * is the operand count's business, not the operator's: the same "-" is a
 * negation for one operand and a subtraction for two.
 *
 * @param op      Operator code.
 * @param n_args  Operands the operator applies to.
 * @return Static string; "" for DCG_OP_NONE, an unknown operator, or an arity
 *         that writes nothing.
 */
static inline const char*          c_dcg_op_code_symbol(dcg_op_code op, size_t n_args) {
    if (op == DCG_OP_NONE) return "";
    if (n_args < 1 || n_args > 2) return ""; /* the if-form and the calls write none */

    size_t variant = (size_t) ((int) op & DCG_OP_VARIANT_MASK);
    if (variant == 0) return "";
    variant--;

    switch ((int) op & DCG_OP_FAMILY_MASK) {
        case DCG_OP_ARITH:
            return c_dcg_name_at(DCG_OP_ARITH_SYMBOLS, sizeof(DCG_OP_ARITH_SYMBOLS) / sizeof(char*), variant);
        case DCG_OP_COMPARE:
            return c_dcg_name_at(DCG_OP_COMPARE_SYMBOLS, sizeof(DCG_OP_COMPARE_SYMBOLS) / sizeof(char*), variant);
        case DCG_OP_LOGIC:
            return c_dcg_name_at(DCG_OP_LOGIC_SYMBOLS, sizeof(DCG_OP_LOGIC_SYMBOLS) / sizeof(char*), variant);
        case DCG_OP_ACCESS:
            return c_dcg_name_at(DCG_OP_ACCESS_SYMBOLS, sizeof(DCG_OP_ACCESS_SYMBOLS) / sizeof(char*), variant);
        default:
            return "";
    }
}

/**
 * @brief Stable display name of an operator code.
 *
 * @param op  Operator code.
 * @return Static string; "UNKNOWN" for an out-of-range operator.
 */
static inline const char* c_dcg_op_code_name(dcg_op_code op) {
    if (op == DCG_OP_NONE) return "NONE";

    size_t variant = (size_t) ((int) op & DCG_OP_VARIANT_MASK);
    if (variant == 0) return "UNKNOWN";
    variant--;  // operator variants are numbered from 1

    switch ((int) op & DCG_OP_FAMILY_MASK) {
        case DCG_OP_ARITH:
            return c_dcg_name_at(DCG_OP_ARITH_NAMES, sizeof(DCG_OP_ARITH_NAMES) / sizeof(char*), variant);
        case DCG_OP_COMPARE:
            return c_dcg_name_at(DCG_OP_COMPARE_NAMES, sizeof(DCG_OP_COMPARE_NAMES) / sizeof(char*), variant);
        case DCG_OP_LOGIC:
            return c_dcg_name_at(DCG_OP_LOGIC_NAMES, sizeof(DCG_OP_LOGIC_NAMES) / sizeof(char*), variant);
        case DCG_OP_ACCESS:
            return c_dcg_name_at(DCG_OP_ACCESS_NAMES, sizeof(DCG_OP_ACCESS_NAMES) / sizeof(char*), variant);
        default:
            return "UNKNOWN";
    }
}
/**
 * @brief The node type an operator code belongs to.
 *
 * @param op  Operator code.
 * @return UNARY / BINARY / TERNARY / CALL as implied by the operator, or
 *         DCG_NODE_OP when the operator is unknown.
 */
static inline dcg_node_type c_dcg_op_code_node_type(dcg_op_code op) {
    switch (op) {
        case DCG_OP_NEG:
        case DCG_OP_NOT:
            return DCG_NODE_UNARY;
        case DCG_OP_ADD:
        case DCG_OP_SUB:
        case DCG_OP_MUL:
        case DCG_OP_DIV:
        case DCG_OP_FLOORDIV:
        case DCG_OP_POW:
        case DCG_OP_EQ:
        case DCG_OP_NE:
        case DCG_OP_GT:
        case DCG_OP_GE:
        case DCG_OP_LT:
        case DCG_OP_LE:
        case DCG_OP_AND:
        case DCG_OP_OR:
        case DCG_OP_ATTR:
        case DCG_OP_GETITEM:
            return DCG_NODE_BINARY;
        default:
            return DCG_NODE_OP;
    }
}

// ========== Composing the Repr ==========

/**
 * @brief The text an input node is written as: its repr, or its value's format.
 *
 * A node carries its repr from the moment it is built, so the fallback is only
 * for a node somebody assembled by hand without one. An expression is written in
 * parentheses: the composed text then reads the way it evaluates.
 *
 * @param input  Input node to write (NULL-safe).
 * @param out    Destination buffer.
 * @param cap    Capacity of out.
 * @return Number of characters written (excluding NUL), or a DCG_ERR_* code.
 */
static inline int c_dcg_node_expr_alias(const dcg_node* input, char* out, size_t cap) {
    if (!input || !out || cap == 0) return DCG_ERR_INVALID_ARG;
    if (!input->repr) return c_dcg_var_format(&input->out, out, cap);

    /* An input that is an expression is written in parentheses, so the composed
     * text reads the way it evaluates: a + (b * c), not a + b * c. */
    int n = c_dcg_node_type_is_op(input->ntype) ? snprintf(out, cap, "(%s)", input->repr) : snprintf(out, cap, "%s", input->repr);
    return n < 0 ? DCG_ERR_FORMAT : n;
}

/**
 * @brief Render an expression the way its operator writes it - the op style.
 *
 * One operand is written after the symbol (-12), two around it (12 - 3), three
 * as the if-form (cond ? then : else), and any other count comma-separated. This
 * is the repr an operator node is given when it is built; a call, which writes a
 * name rather than a symbol, takes c_dcg_node_expr_func_style() instead.
 *
 * @param inputs    Input nodes, in operand order.
 * @param n_inputs  Number of inputs.
 * @param op        Operator code.
 * @param out       Destination buffer.
 * @param cap       Capacity of out.
 * @return Number of characters written (excluding NUL), or a DCG_ERR_* code -
 *         a positive answer, so a caller checks for a negative one.
 */
static inline int c_dcg_node_expr_op_style(dcg_node* const* inputs, size_t n_inputs, dcg_op_code op, char* out, size_t cap) {
    if (!inputs || !out || cap == 0 || n_inputs == 0) return DCG_ERR_INVALID_ARG;

    const char* token = c_dcg_op_code_symbol(op, n_inputs);
    char        alias[DCG_NODE_STRING_MAXLEN];
    dcg_strbuf  buf;

    c_dcg_sb_init(&buf, out, cap);

    for (size_t i = 0; i < n_inputs; i++) {
        if (i == 0) {
            if (n_inputs == 1) c_dcg_sb_puts(&buf, token); /* -12: the token in front */
        }
        else if (n_inputs == 2) c_dcg_sb_printf(&buf, " %s ", token); /* 12 - 3: between */
        else if (n_inputs == 3 && i == 1) c_dcg_sb_puts(&buf, " ? ");
        else if (n_inputs == 3 && i == 2) c_dcg_sb_puts(&buf, " : ");
        else c_dcg_sb_puts(&buf, ", ");

        if (c_dcg_node_expr_alias(inputs[i], alias, sizeof(alias)) < 0) return DCG_ERR_FORMAT;
        c_dcg_sb_puts(&buf, alias);
    }

    return buf.used < buf.cap ? (int) buf.used : DCG_ERR_FULL;
}

/**
 * @brief Render an expression the way a function writes it - the func style.
 *
 * NAME(a, b): the name is the caller's - a call is written as the callee it calls
 * - and falls back to the operator's own name. This is the repr a call node is
 * given when it is built, and the one to ask for when a name reads better than a
 * symbol.
 *
 * @param inputs    Input nodes, in operand order.
 * @param n_inputs  Number of inputs.
 * @param op        Operator code (named when `name` is NULL).
 * @param name      The function name to write (may be NULL).
 * @param out       Destination buffer.
 * @param cap       Capacity of out.
 * @return Number of characters written (excluding NUL), or a DCG_ERR_* code -
 *         a positive answer, so a caller checks for a negative one.
 */
static inline int c_dcg_node_expr_func_style(dcg_node* const* inputs, size_t n_inputs, dcg_op_code op, const char* name, char* out, size_t cap) {
    if (!inputs || !out || cap == 0 || n_inputs == 0) return DCG_ERR_INVALID_ARG;

    char       alias[DCG_NODE_STRING_MAXLEN];
    dcg_strbuf buf;

    c_dcg_sb_init(&buf, out, cap);
    c_dcg_sb_printf(&buf, "%s(", name ? name : c_dcg_op_code_name(op));

    for (size_t i = 0; i < n_inputs; i++) {
        if (c_dcg_node_expr_alias(inputs[i], alias, sizeof(alias)) < 0) return DCG_ERR_FORMAT;
        if (i > 0) c_dcg_sb_puts(&buf, ", ");
        c_dcg_sb_puts(&buf, alias);
    }
    c_dcg_sb_puts(&buf, ")");

    return buf.used < buf.cap ? (int) buf.used : DCG_ERR_FULL;
}

// ========== Lifecycle Methods ==========

/**
 * @brief Allocate an expression node with room for its operands.
 *
 * The block is sized for the operands it will hold, so the count is fixed for
 * the lifetime of the node - which is what a baked graph wants: the operands
 * are known when the node is built. The operand slots start empty; bind them
 * with c_dcg_node_expr_bind() or use a typed constructor, which fills them.
 *
 * @param n_args     Number of operands (clamped to DCG_EXPR_DEFAULT_ARGS if 0).
 * @param ntype      An operator type (UNARY / BINARY / TERNARY / CALL, or the head).
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM / invalid type.
 */
static inline dcg_expression_node* c_dcg_node_new_expr(size_t n_args, dcg_node_type ntype, allocator_protocol* allocator) {
    if (!c_dcg_node_type_is_op(ntype)) return NULL;
    if (n_args == 0) n_args = DCG_EXPR_DEFAULT_ARGS;

    dcg_expression_node* node = (dcg_expression_node*) c_ap_alloc(sizeof(dcg_expression_node) + (n_args * (sizeof(dcg_var_t) + sizeof(dcg_node*))), allocator);
    if (!node) return NULL;

    if (c_dcg_node_init(&node->base, ntype, NULL) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }

    node->op         = DCG_OP_NONE;
    node->n_args     = n_args;
    node->components = c_dcg_node_expr_components(node);

    for (size_t i = 0; i < n_args; i++) (void) c_dcg_var_init(&node->args[i]);

    return node;
}

/**
 * @brief Tear down an expression node and free its buf.
 *
 * The operand slots and the pointer array both sit in blocks of the node's own,
 * so they go with it. What has to be given back first is the hold the node took
 * on its operands: an operand nothing else holds is released here, and one that
 * outlives the expression is simply left with one fewer reference.
 *
 * @param node  Node to free (NULL-safe).
 */
static inline void c_dcg_node_free_expr(dcg_expression_node* node) {
    if (!node) return;

    for (size_t i = 0; i < node->n_args; i++) {
        if (node->components[i]) c_ap_decref(node->components[i]);
    }
    c_dcg_node_free(&node->base);
}

// ========== Public APIs - Typed Constructors ==========

/**
 * @brief Allocate an expression over one input node.
 *
 * @param op         Operator code (e.g. DCG_OP_NEG, DCG_OP_NOT).
 * @param src        Input node the operand refers to.
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM / invalid type / a missing input.
 */
static inline dcg_expression_node* c_dcg_node_new_expr_unary(dcg_op_code op, dcg_node* src, allocator_protocol* allocator) {
    if (!src) return NULL;

    dcg_expression_node* node = c_dcg_node_new_expr(1, DCG_NODE_UNARY, allocator);
    if (!node) return NULL;

    node->op = op;
    if (c_dcg_node_expr_bind(node, 0, src) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }

    dcg_node* inputs[1] = {src};
    char      repr[DCG_NODE_STRING_MAXLEN];

    if (c_dcg_node_expr_op_style(inputs, 1, op, repr, sizeof(repr)) < 0 || c_dcg_node_set_repr(&node->base, repr) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }
    return node;
}

/**
 * @brief Allocate an expression over two input nodes, the left one first.
 *
 * @param op         Operator code (arithmetic, comparison, logical or access).
 * @param var_0      Left input node.
 * @param var_1      Right input node.
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM / invalid type / a missing input.
 */
static inline dcg_expression_node* c_dcg_node_new_expr_binary(dcg_op_code op, dcg_node* var_0, dcg_node* var_1, allocator_protocol* allocator) {
    if (!var_0 || !var_1) return NULL;

    dcg_expression_node* node = c_dcg_node_new_expr(2, DCG_NODE_BINARY, allocator);
    if (!node) return NULL;

    node->op = op;
    if (c_dcg_node_expr_bind(node, 0, var_0) != DCG_OK || c_dcg_node_expr_bind(node, 1, var_1) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }

    dcg_node* inputs[2] = {var_0, var_1};
    char      repr[DCG_NODE_STRING_MAXLEN];

    if (c_dcg_node_expr_op_style(inputs, 2, op, repr, sizeof(repr)) < 0 || c_dcg_node_set_repr(&node->base, repr) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }
    return node;
}

/**
 * @brief Allocate an expression over three input nodes (cond, then, else).
 *
 * @param op         Operator code (DCG_OP_NONE for the plain if-expression).
 * @param var_0      Condition input node.
 * @param var_1      Input node taken when the condition holds.
 * @param var_2      Input node taken when it does not.
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM / invalid type / a missing input.
 */
static inline dcg_expression_node* c_dcg_node_new_expr_ternary(dcg_op_code op, dcg_node* var_0, dcg_node* var_1, dcg_node* var_2, allocator_protocol* allocator) {
    if (!var_0 || !var_1 || !var_2) return NULL;

    dcg_expression_node* node = c_dcg_node_new_expr(3, DCG_NODE_TERNARY, allocator);
    if (!node) return NULL;

    node->op = op;
    for (size_t i = 0; i < 3; i++) {
        dcg_node* input = i == 0 ? var_0 : (i == 1 ? var_1 : var_2);
        if (c_dcg_node_expr_bind(node, i, input) != DCG_OK) {
            c_ap_free_owned(node);
            return NULL;
        }
    }

    dcg_node* inputs[3] = {var_0, var_1, var_2};
    char      repr[DCG_NODE_STRING_MAXLEN];

    if (c_dcg_node_expr_op_style(inputs, 3, op, repr, sizeof(repr)) < 0 || c_dcg_node_set_repr(&node->base, repr) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }
    return node;
}

/**
 * @brief Allocate a variadic call over an array of input nodes.
 *
 * A call has no symbol of its own, so its repr is given rather than inferred.
 *
 * @param op         Operator code.
 * @param vars       Input nodes, in order (at least one).
 * @param n_vars     Number of input nodes.
 * @param repr       The callee's name; the repr is composed from it (may be NULL).
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM / invalid type / a missing input.
 */
static inline dcg_expression_node* c_dcg_node_new_expr_call(dcg_op_code op, dcg_node** vars, size_t n_vars, const char* repr, allocator_protocol* allocator) {
    if (!vars || n_vars == 0) return NULL;

    dcg_expression_node* node = c_dcg_node_new_expr(n_vars, DCG_NODE_CALL, allocator);
    if (!node) return NULL;

    node->op = op;
    for (size_t i = 0; i < n_vars; i++) {
        if (c_dcg_node_expr_bind(node, i, vars[i]) != DCG_OK) {
            c_ap_free_owned(node);
            return NULL;
        }
    }
    /* A call is written as the function it calls: the repr the caller gives is
     * the callee's name, and the operands are written as its arguments. */
    char composed[DCG_NODE_STRING_MAXLEN];

    if (c_dcg_node_expr_func_style(vars, n_vars, op, repr, composed, sizeof(composed)) < 0 || c_dcg_node_set_repr(&node->base, composed) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }
    return node;
}

// ========== Public APIs - Operands ==========

/**
 * @brief Bind an input node to one operand slot.
 *
 * What lands in the slot depends on what the input is:
 *
 *   - a variable node already refers to the value it reflects, so its out is
 *     taken as it is: the operand is that same reference, one hop from the value;
 *   - a constant is known when the graph is baked, so its VALUE is folded in -
 *     a string is copied into a block nested under the expression, and a scalar,
 *     pointer or container is stored as it is;
 *   - anything else is a node whose value is produced at evaluation time, so the
 *     slot becomes a reference to its out slot and reads it when read.
 *
 * The input must outlive the expression in the second case, as any reference
 * requires; in the first case the expression is self-contained.
 *
 * @param node   Expression to modify.
 * @param index  Operand index.
 * @param input  Node whose value the operand takes.
 * @return DCG_OK, DCG_ERR_INVALID_ARG, DCG_ERR_RANGE or DCG_ERR_OOM.
 */
static inline int c_dcg_node_expr_bind(dcg_expression_node* node, size_t index, dcg_node* input) {
    if (!node || !input) return DCG_ERR_INVALID_ARG;
    if (index >= node->n_args) return DCG_ERR_RANGE;

    dcg_var_t* slot     = &node->args[index];
    int        ret_code = DCG_OK;
    int        refers   = 0;

    /* A variable node holds no value of its own: its out is already a reference
     * to the value it reflects. Taking it as it is keeps the operand one hop
     * from the value - wrapping it would make a reference to a reference and
     * charge every read an indirection that reads nothing. */
    if (input->ntype == DCG_NODE_VARIABLE) {
        *slot = input->out;
    }
    else if (c_dcg_node_type_is_input(input->ntype)) {
        if (input->out.dtype != VAR_TYPE_STRING || !input->out.value.as_string) {
            *slot = input->out; /* a value, borrowed as the constant holds it */
        }
        else {
            size_t len  = strlen(input->out.value.as_string);
            char*  copy = (char*) c_ap_alloc_child(len + 1, NULL, node);
            if (!copy) return DCG_ERR_OOM;
            memcpy(copy, input->out.value.as_string, len + 1);
            ret_code = c_dcg_var_init_string(slot, copy);
        }
    }
    else {
        ret_code = c_dcg_var_init_ref(slot, &input->out);
        refers   = 1;
    }
    if (ret_code != DCG_OK) return ret_code;

    /* The hold goes on last, so a slot that could not be filled leaves the
     * previous operand standing.
     *
     * Only a slot that refers to the operand's storage takes a reference. A slot
     * that took the operand's VALUE does not need the node afterwards - that is
     * what folding is for, and it is what lets a caller release a literal the
     * moment it is bound. `components[index]` is therefore the operand this
     * expression holds alive, or NULL when the slot is self-contained.
     *
     * The new reference is taken before the old one is given back: rebinding a
     * slot to the operand it already holds, or two expressions trading one, must
     * not drop the last reference between the two steps. */
    dcg_node* previous = node->components[index];
    if (previous == input) return DCG_OK;

    if (previous) c_ap_decref(previous);
    node->components[index] = NULL;
    if (refers) {
        c_ap_incref(input);
        node->components[index] = input;
    }
    return DCG_OK;
}

#endif  // C_DCG_BAKE_EXPR_H
