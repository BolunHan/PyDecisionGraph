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

// clang-format off

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
 * The base node must stay the FIRST member: a dcg_expression_node* is therefore
 * a valid dcg_node*.
 */
typedef struct dcg_expression_node {
    dcg_node    base;    // The common node header. Must stay first.
    dcg_op_code op;      // The operator applied to the operands.
    size_t      n_args;  // Number of operands in `args`.
    dcg_var_t   args[];  // Operand slots, n_args of them.
} dcg_expression_node;

// clang-format on

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

// Payload teardown, registered with the base by every constructor above
static inline void                 c_dcg_node_expr_variant_dealloc(dcg_node* node);

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
 * @brief The symbol of an operator ("+", "==", ...), for building a repr.
 *
 * @param op  Operator code.
 * @return Static string; "" for DCG_OP_NONE or an unknown operator.
 */
static inline const char*          c_dcg_op_code_symbol(dcg_op_code op) {
    if (op == DCG_OP_NONE) return "";

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
 * @brief The node kind an operator code belongs to.
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
 * @param ntype      An operator kind (UNARY / BINARY / TERNARY / CALL, or the head).
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM / invalid kind.
 */
static inline dcg_expression_node* c_dcg_node_new_expr(size_t n_args, dcg_node_type ntype, allocator_protocol* allocator) {
    if (!c_dcg_node_type_is_op(ntype)) return NULL;
    if (n_args == 0) n_args = DCG_EXPR_DEFAULT_ARGS;

    dcg_expression_node* node = (dcg_expression_node*) c_ap_alloc(sizeof(dcg_expression_node) + (n_args * sizeof(dcg_var_t)), allocator);
    if (!node) return NULL;

    if (c_dcg_node_init(&node->base, ntype, NULL) != DCG_OK) {
        c_ap_free_owned(node);
        return NULL;
    }

    node->op     = DCG_OP_NONE;
    node->n_args = n_args;
    for (size_t i = 0; i < n_args; i++) (void) c_dcg_var_init(&node->args[i]);

    /* The base cannot see the operand array; this is how it releases it. */
    node->base.fn_variant_dealloc = c_dcg_node_expr_variant_dealloc;
    return node;
}

/**
 * @brief Tear down an expression node and free its buf.
 *
 * The operand copies go first - the base teardown runs the variant hook before
 * it zeroes the header - and a reference operand owns nothing to release.
 *
 * @param node  Node to free (NULL-safe).
 */
static inline void c_dcg_node_free_expr(dcg_expression_node* node) {
    if (!node) return;
    c_dcg_node_free(&node->base);
}

/**
 * @brief Release the operand array of an expression - the base variant hook.
 *
 * @param node  The node being torn down (a dcg_node* that is really the variant).
 */
static inline void c_dcg_node_expr_variant_dealloc(dcg_node* node) {
    dcg_expression_node* expr = (dcg_expression_node*) node;

    for (size_t i = 0; i < expr->n_args; i++) {
        if (expr->args[i].dtype == VAR_TYPE_STRING && expr->args[i].value.as_string) {
            c_ap_free_owned((void*) expr->args[i].value.as_string); /* a folded string copy */
        }
        (void) c_dcg_var_init(&expr->args[i]);
    }
    expr->op     = DCG_OP_NONE;
    expr->n_args = 0;
}

// ========== Public APIs - Typed Constructors ==========

/**
 * @brief Allocate an expression over one input node.
 *
 * @param op         Operator code (e.g. DCG_OP_NEG, DCG_OP_NOT).
 * @param src        Input node the operand refers to.
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM / invalid kind / a missing input.
 */
static inline dcg_expression_node* c_dcg_node_new_expr_unary(dcg_op_code op, dcg_node* src, allocator_protocol* allocator) {
    if (!src) return NULL;

    dcg_expression_node* node = c_dcg_node_new_expr(1, DCG_NODE_UNARY, allocator);
    if (!node) return NULL;

    node->op = op;
    if (c_dcg_node_expr_bind(node, 0, src) != DCG_OK || c_dcg_node_set_repr(&node->base, c_dcg_op_code_symbol(op)) != DCG_OK) {
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
 * @return The node, or NULL on OOM / invalid kind / a missing input.
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
    if (c_dcg_node_set_repr(&node->base, c_dcg_op_code_symbol(op)) != DCG_OK) {
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
 * @return The node, or NULL on OOM / invalid kind / a missing input.
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
    if (c_dcg_node_set_repr(&node->base, c_dcg_op_code_symbol(op)) != DCG_OK) {
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
 * @param repr       Display text to copy (may be NULL).
 * @param allocator  Allocator for the block; NULL falls back to the plain heap.
 * @return The node, or NULL on OOM / invalid kind / a missing input.
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
    if (c_dcg_node_set_repr(&node->base, repr) != DCG_OK) {
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

    dcg_var_t* slot = &node->args[index];

    if (c_dcg_node_type_is_const(input->ntype)) {
        if (input->out.dtype != VAR_TYPE_STRING || !input->out.value.as_string) {
            *slot = input->out; /* a value, borrowed as the constant holds it */
            return DCG_OK;
        }

        size_t len  = strlen(input->out.value.as_string);
        char*  copy = (char*) c_ap_alloc_child(len + 1, NULL, node);
        if (!copy) return DCG_ERR_OOM;
        memcpy(copy, input->out.value.as_string, len + 1);
        return c_dcg_var_init_string(slot, copy);
    }

    return c_dcg_var_init_ref(slot, &input->out);
}

#endif  // C_DCG_BAKE_EXPR_H
