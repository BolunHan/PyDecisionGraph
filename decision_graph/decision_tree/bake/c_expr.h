#ifndef C_DCG_BAKE_EXPR_H
#define C_DCG_BAKE_EXPR_H

#include <math.h>
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

/*
 * The shape of the flat rule list (DCG_EXPR_EVAL_FNS): one slot per family, and
 * one family's worth of slots for the variants. Operator codes number their
 * variants from 1, so a family needs a slot for the family itself (variant 0)
 * and room for the highest variant it writes - 7, which is arithmetic's NEG.
 */
#ifndef DCG_OP_FAMILIES
#define DCG_OP_FAMILIES 5U
#endif

#ifndef DCG_OP_FAMILY_SLOTS
#define DCG_OP_FAMILY_SLOTS 8U
#endif

#if (DCG_OP_NEG & DCG_OP_VARIANT_MASK) >= DCG_OP_FAMILY_SLOTS
#error "DCG_OP_FAMILY_SLOTS must cover the operator variants (see DCG_OP_NEG)"
#endif

#if ((DCG_OP_ACCESS & DCG_OP_FAMILY_MASK) >> 8) >= DCG_OP_FAMILIES
#error "DCG_OP_FAMILIES must cover the operator families (see DCG_OP_ACCESS)"
#endif

// ========== Structs ==========

/**
 * @brief Applying one operator: the arithmetic of a single op, in the one shape
 * they all share.
 *
 * This is the KERNEL, not the rule. It takes the operands and produces the
 * value, and its callers are the operator's own evaluation
 * (c_dcg_node_expr_eval_<op>) and the two runtime dispatchers a caller with a
 * code and no node reaches (c_dcg_node_expr_apply_unary /
 * c_dcg_node_expr_apply_binary). Which kernel applies an operator is settled
 * where the operator is known, never searched for later - see
 * c_dcg_node_expr_set_op and c_dcg_node_expr_apply_fn_of.
 *
 * `b` is the second operand for a binary and untouched for a unary - the two
 * arities share one signature so one kernel serves either, and a unary kernel is
 * the only thing that may ignore it.
 *
 * @param out  Receives the value.
 * @param a    Left operand (the only operand for a unary).
 * @param b    Right operand, or NULL to a unary kernel.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
typedef int (*dcg_expr_apply_fn)(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b);

/**
 * @brief An expression node: the base node plus its operator and its operands.
 *
 * The operands are VALUES, not child nodes - they sit inline in the block,
 * which is what makes evaluating an expression a loop over a flat array rather
 * than a walk of the graph. A graph keeps its edges for control flow; an
 * expression keeps its operands here.
 *
 * `args` is a flexible array member, so the block is allocated with exactly as
 * many operands as it needs and the node is never an embedded field. Each slot is
 * filled by binding an input node (c_dcg_node_expr_bind): a reference to the
 * input's out slot, or the folded value of a constant input; a string value
 * folded in is a copy nested under the node, and a reference owns nothing.
 *
 * At RUN time those slots are a WORKSPACE: the walk evaluates each component,
 * dereferences what it reads as, and writes the result over the slot before the
 * operator is applied. What a binding left there is overwritten on every
 * evaluation, so nothing may read the slots as a record of what the node was
 * built from - the components array is that.
 *
 * `components` is the node's hold on the operands it was given, one per slot and
 * one reference each - no exceptions, so there is no case to reason about and
 * the array answers "what was this operand made from?" for every slot alike.
 * `c_dcg_node_expr_bind` takes the reference and teardown gives it back
 * (c_ap_decref), so a caller that lets go of an operand it bound leaves the
 * expression working, and one that bound a live read leaves it something to ask
 * when the entry's type arrives. The array is n_args entries - exactly as many
 * as there are slots, which is why there is no capacity field - and it sits in
 * the node's OWN block, right behind the operand slots, so the node has no
 * separate block to release and the array goes where the node goes.
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

// Running a node - the protocol entry the operand step drives (defined in c_eval.h,
// which is above this family: the declaration is what makes the cycle safe)
static inline int                  c_dcg_node_eval(dcg_node* node, bool inplace);

// Evaluation - what this family's nodes evaluate to
static inline int                  c_dcg_node_expr_set_op(dcg_expression_node* node, dcg_op_code op);
static inline size_t               c_dcg_op_code_index(dcg_op_code op);
static inline dcg_expr_apply_fn    c_dcg_node_expr_apply_fn_of(dcg_node_type ntype, dcg_op_code op);
static inline int                  c_dcg_node_expr_apply_unary(dcg_var_t* out, dcg_op_code op, const dcg_var_t* a);
static inline int                  c_dcg_node_expr_apply_binary(dcg_var_t* out, dcg_op_code op, const dcg_var_t* a, const dcg_var_t* b);

// Operands at run time - the workspace, filled from this node's own components
static inline int                  c_dcg_node_expr_eval_operand(dcg_expression_node* node, size_t index);
static inline int                  c_dcg_node_expr_eval_operands(dcg_expression_node* node);

// Applying - the arithmetic of ONE operator, one kernel each
static inline int                  c_dcg_node_expr_apply_refuse(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b);
static inline int                  c_dcg_node_expr_apply_neg(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b);
static inline int                  c_dcg_node_expr_apply_not(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b);
static inline int                  c_dcg_node_expr_apply_add(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b);
static inline int                  c_dcg_node_expr_apply_sub(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b);
static inline int                  c_dcg_node_expr_apply_mul(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b);
static inline int                  c_dcg_node_expr_apply_div(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b);
static inline int                  c_dcg_node_expr_apply_floordiv(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b);
static inline int                  c_dcg_node_expr_apply_pow(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b);
static inline int                  c_dcg_node_expr_apply_eq(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b);
static inline int                  c_dcg_node_expr_apply_ne(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b);
static inline int                  c_dcg_node_expr_apply_gt(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b);
static inline int                  c_dcg_node_expr_apply_ge(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b);
static inline int                  c_dcg_node_expr_apply_lt(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b);
static inline int                  c_dcg_node_expr_apply_le(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b);
static inline int                  c_dcg_node_expr_apply_and(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b);
static inline int                  c_dcg_node_expr_apply_or(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b);

// Evaluating - the rule of ONE operator, one function each (the flat list of
// them is DCG_EXPR_EVAL_FNS), and the entry that finds one from the code
static inline dcg_node_hook_fn     c_dcg_node_expr_eval_of_arity(dcg_node_type ntype);
static inline int                  c_dcg_node_expr_eval(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_refuse(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_unknown(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_ternary(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_call(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_neg(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_not(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_add(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_sub(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_mul(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_div(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_floordiv(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_pow(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_eq(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_ne(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_gt(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_ge(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_lt(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_le(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_and(dcg_node* node, void* user_data);
static inline int                  c_dcg_node_expr_eval_or(dcg_node* node, void* user_data);

// Composing the repr
static inline int                  c_dcg_node_expr_alias(const dcg_node* input, char* out, size_t cap);
static inline int                  c_dcg_node_expr_op_style(dcg_node* const* inputs, size_t n_inputs, dcg_op_code op, char* out, size_t cap);
static inline int                  c_dcg_node_expr_func_style(dcg_node* const* inputs, size_t n_inputs, dcg_op_code op, const char* name, char* out, size_t cap);

/*
 * The protocol, for ONE thing only: an expression's operands are other NODES, so
 * producing their values means running them, and running a node is the
 * evaluator's (c_dcg_node_eval). This family owns the operand loop - the slots
 * and the pointers are its own fields - and the protocol owns what running a node
 * means.
 *
 * c_eval.h includes every family, so this is a cycle in the include graph. It is
 * a CONTAINED one - the protocol needs nothing from this family but the entry
 * below, and this header needs nothing from the protocol but c_dcg_node_eval -
 * and it is safe because of where this include sits: BELOW this family's
 * declarations, so that a translation unit entering through this header has them
 * by the time the protocol parses, and a translation unit entering through the
 * protocol gets them when it reaches this header from the other side. The
 * forward declaration below is what makes the second order work.
 */
#include <decision_graph/decision_tree/bake/c_eval.h>

// ========== Operator Utilities ==========

/*
 * Operator names, one table per family. The variant numbers start at 1, so the
 * table index is the variant minus one.
 */
static const char* const  DCG_OP_ARITH_NAMES[]   = {"ADD", "SUB", "MUL", "DIV", "FLOORDIV", "POW", "NEG"};
static const char* const  DCG_OP_COMPARE_NAMES[] = {"EQ", "NE", "GT", "GE", "LT", "LE"};
static const char* const  DCG_OP_LOGIC_NAMES[]   = {"AND", "OR", "NOT"};
static const char* const  DCG_OP_ACCESS_NAMES[]  = {"ATTR", "GETITEM"};

/*
 * Operator symbols, one table per family, mirroring the capi's op_repr - the
 * form a node's repr is built from.
 */
static const char* const  DCG_OP_ARITH_SYMBOLS[]   = {"+", "-", "*", "/", "//", "**", "-"};
static const char* const  DCG_OP_COMPARE_SYMBOLS[] = {"==", "!=", ">", ">=", "<", "<="};
static const char* const  DCG_OP_LOGIC_SYMBOLS[]   = {"&", "|", "~"};
static const char* const  DCG_OP_ACCESS_SYMBOLS[]  = {".", "[]"};

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
static inline const char* c_dcg_op_code_symbol(dcg_op_code op, size_t n_args) {
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

    /* Every expression node is built here, so this is the one place the family's
     * rule is injected - the same way every other family's constructor injects
     * its own (see DCG_EVAL_DIRECT_HOOKS): the arity's rule, since no operator is
     * set yet. The constructors that take an operator hand it over a moment
     * later, and the rule goes with it (c_dcg_node_expr_set_op). A build with the
     * injection off installs nothing and the dispatch finds the rule instead. */
#if DCG_EVAL_DIRECT_HOOKS
    node->base.eval_ctx.type_eval_fn = c_dcg_node_expr_eval_of_arity(ntype);
#endif
    (void) c_dcg_node_expr_set_op(node, DCG_OP_NONE);
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
 * The reference each operand holds is given back HERE, once per slot, and that is
 * the whole of it: the hold was taken by c_dcg_node_expr_bind() and this is the
 * one place it is returned, so the count a bind raised is the count a free
 * lowers and nothing double-counts. When it was the last reference the block is
 * released by the allocator itself, which takes the ownership tree with it.
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

// ========== Applying One Operator ==========

/*
 * The kernels: one operator's arithmetic, taking the operands and producing the
 * value. They live here, with the operator tables they are named for, and they
 * are reached two ways - by the operator's own evaluation rule
 * (c_dcg_node_expr_eval_<op>, the way a node is evaluated) and by the runtime
 * dispatchers a caller with a code and no node uses
 * (c_dcg_node_expr_apply_unary / c_dcg_node_expr_apply_binary). So a kernel
 * cannot be right by one way and wrong by the other: there is one kernel each,
 * and both ways call it.
 */

/**
 * @brief The kernel of an operator this arity has no kernel for: saying so is
 * the kernel.
 *
 * Every op has a function in each arity - the ones that can apply it and this
 * one, which refuses it - so the apply-side lookup never answers NULL for an
 * arity that has operators, and its callers never have to ask.
 *
 * @param out  Receives nothing: left as it was.
 * @return DCG_ERR_TYPE.
 */
static inline int c_dcg_node_expr_apply_refuse(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b) {
    (void) a;
    (void) b;
    (void) c_dcg_var_init(out);
    return DCG_ERR_TYPE;
}

/**
 * @brief Negation: a number, negated.
 *
 * @param out  Receives the value.
 * @param a    The operand.
 * @param b    Unused (see dcg_expr_apply_fn).
 * @return DCG_OK, or DCG_ERR_MATH for an operand that is not a number.
 */
static inline int c_dcg_node_expr_apply_neg(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b) {
    (void) b;
    (void) c_dcg_var_init(out);

    switch (c_dcg_var_numeric_of(a)) {
        case VAR_NUMERIC_INT:
            return c_dcg_var_init_int(out, -c_dcg_var_as_int(a));
        case VAR_NUMERIC_DOUBLE:
            return c_dcg_var_init_double(out, -c_dcg_var_as_double(a));
        default:
            return DCG_ERR_MATH;
    }
}

/**
 * @brief Logical not: the operand's truth, negated.
 *
 * @param out  Receives the value.
 * @param a    The operand.
 * @param b    Unused.
 * @return DCG_OK.
 */
static inline int c_dcg_node_expr_apply_not(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b) {
    (void) b;
    (void) c_dcg_var_init(out);
    return c_dcg_var_init_bool(out, !c_dcg_var_is_truthy(a));
}

/**
 * @brief Addition: two numbers, added.
 *
 * The result follows the operands: a whole-number pair stays whole, and one
 * double in the pair makes the result a double. Integer arithmetic is 64-bit and
 * wraps the way the C it is written in does; Python's arbitrary-precision ints
 * are not mirrored.
 *
 * @param out  Receives the value.
 * @param a    Left operand.
 * @param b    Right operand.
 * @return DCG_OK, or DCG_ERR_MATH for an operand that is not a number.
 */
static inline int c_dcg_node_expr_apply_add(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b) {
    (void) c_dcg_var_init(out);

    dcg_var_numeric left  = c_dcg_var_numeric_of(a);
    dcg_var_numeric right = c_dcg_var_numeric_of(b);
    if (left == VAR_NUMERIC_NONE || right == VAR_NUMERIC_NONE) return DCG_ERR_MATH;

    if (left == VAR_NUMERIC_DOUBLE || right == VAR_NUMERIC_DOUBLE) return c_dcg_var_init_double(out, c_dcg_var_as_double(a) + c_dcg_var_as_double(b));
    return c_dcg_var_init_int(out, c_dcg_var_as_int(a) + c_dcg_var_as_int(b));
}

/**
 * @brief Subtraction: two numbers, subtracted.
 *
 * @param out  Receives the value.
 * @param a    Left operand.
 * @param b    Right operand.
 * @return DCG_OK, or DCG_ERR_MATH for an operand that is not a number.
 */
static inline int c_dcg_node_expr_apply_sub(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b) {
    (void) c_dcg_var_init(out);

    dcg_var_numeric left  = c_dcg_var_numeric_of(a);
    dcg_var_numeric right = c_dcg_var_numeric_of(b);
    if (left == VAR_NUMERIC_NONE || right == VAR_NUMERIC_NONE) return DCG_ERR_MATH;

    if (left == VAR_NUMERIC_DOUBLE || right == VAR_NUMERIC_DOUBLE) return c_dcg_var_init_double(out, c_dcg_var_as_double(a) - c_dcg_var_as_double(b));
    return c_dcg_var_init_int(out, c_dcg_var_as_int(a) - c_dcg_var_as_int(b));
}

/**
 * @brief Multiplication: two numbers, multiplied.
 *
 * @param out  Receives the value.
 * @param a    Left operand.
 * @param b    Right operand.
 * @return DCG_OK, or DCG_ERR_MATH for an operand that is not a number.
 */
static inline int c_dcg_node_expr_apply_mul(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b) {
    (void) c_dcg_var_init(out);

    dcg_var_numeric left  = c_dcg_var_numeric_of(a);
    dcg_var_numeric right = c_dcg_var_numeric_of(b);
    if (left == VAR_NUMERIC_NONE || right == VAR_NUMERIC_NONE) return DCG_ERR_MATH;

    if (left == VAR_NUMERIC_DOUBLE || right == VAR_NUMERIC_DOUBLE) return c_dcg_var_init_double(out, c_dcg_var_as_double(a) * c_dcg_var_as_double(b));
    return c_dcg_var_init_int(out, c_dcg_var_as_int(a) * c_dcg_var_as_int(b));
}

/**
 * @brief Division: two numbers, divided as floats - including a zero divisor,
 * which is refused.
 *
 * Python's `/` is a float division and refuses a zero divisor, so this one is a
 * double and refuses it too: an infinity travels on through the graph as if it
 * were a price.
 *
 * @param out  Receives the value.
 * @param a    Left operand (the dividend).
 * @param b    Right operand (the divisor).
 * @return DCG_OK, or DCG_ERR_MATH.
 */
static inline int c_dcg_node_expr_apply_div(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b) {
    (void) c_dcg_var_init(out);

    if (c_dcg_var_numeric_of(a) == VAR_NUMERIC_NONE || c_dcg_var_numeric_of(b) == VAR_NUMERIC_NONE) return DCG_ERR_MATH;

    double y = c_dcg_var_as_double(b);
    if (y == 0.0) return DCG_ERR_MATH;
    return c_dcg_var_init_double(out, c_dcg_var_as_double(a) / y);
}

/**
 * @brief Floor division: the quotient floored toward minus infinity, as Python's
 * `//` is - and a zero divisor is refused.
 *
 * @param out  Receives the value.
 * @param a    Left operand (the dividend).
 * @param b    Right operand (the divisor).
 * @return DCG_OK, or DCG_ERR_MATH.
 */
static inline int c_dcg_node_expr_apply_floordiv(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b) {
    (void) c_dcg_var_init(out);

    dcg_var_numeric left  = c_dcg_var_numeric_of(a);
    dcg_var_numeric right = c_dcg_var_numeric_of(b);
    if (left == VAR_NUMERIC_NONE || right == VAR_NUMERIC_NONE) return DCG_ERR_MATH;

    if (left == VAR_NUMERIC_DOUBLE || right == VAR_NUMERIC_DOUBLE) {
        double y = c_dcg_var_as_double(b);
        if (y == 0.0) return DCG_ERR_MATH;
        return c_dcg_var_init_double(out, floor(c_dcg_var_as_double(a) / y));
    }

    ssize_t y = c_dcg_var_as_int(b);
    if (y == 0) return DCG_ERR_MATH;

    ssize_t x         = c_dcg_var_as_int(a);
    ssize_t quotient  = x / y;
    ssize_t remainder = x % y;
    // Python floors toward minus infinity; C truncates toward zero.
    if (remainder != 0 && ((remainder < 0) != (y < 0))) quotient--;
    return c_dcg_var_init_int(out, quotient);
}

/**
 * @brief Exponentiation: a whole exponent over whole numbers stays whole.
 *
 * A whole exponent over whole numbers is raised by squaring, which keeps an
 * integer to the power of an integer an integer - as Python does. Everything
 * else - a fractional exponent, a double operand - is a real power, and comes
 * from the platform's own. A zero base with a negative exponent is the one real
 * power Python refuses, and this refuses it too.
 *
 * @param out  Receives the value.
 * @param a    Left operand (the base).
 * @param b    Right operand (the exponent).
 * @return DCG_OK, or DCG_ERR_MATH.
 */
static inline int c_dcg_node_expr_apply_pow(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b) {
    (void) c_dcg_var_init(out);

    dcg_var_numeric left  = c_dcg_var_numeric_of(a);
    dcg_var_numeric right = c_dcg_var_numeric_of(b);
    if (left == VAR_NUMERIC_NONE || right == VAR_NUMERIC_NONE) return DCG_ERR_MATH;

    double  x        = c_dcg_var_as_double(a);
    double  y        = c_dcg_var_as_double(b);
    ssize_t exponent = (ssize_t) y;

    if (c_dcg_var_ref_base(b->dtype) != VAR_TYPE_DOUBLE && y == (double) exponent && left == VAR_NUMERIC_INT) {
        if (exponent >= 0) {
            ssize_t result = 1;
            ssize_t base   = c_dcg_var_as_int(a);
            for (ssize_t e = exponent; e > 0; e >>= 1) {
                if (e & 1) result *= base;
                if (e > 1) base *= base;
            }
            return c_dcg_var_init_int(out, result);
        }
        return c_dcg_var_init_double(out, 1.0 / pow(x, (double) -exponent));
    }

    if (x == 0.0 && y < 0.0) return DCG_ERR_MATH;
    return c_dcg_var_init_double(out, pow(x, y));
}

/**
 * @brief Equality: the operands compared by VALUE and not by tag.
 *
 * `2 == 2.0` holds here, as it does in Python, where an edge's own match does not
 * (see c_dcg_var_equals - an edge is identified by what its value IS).
 *
 * @param out  Receives the value.
 * @param a    Left operand.
 * @param b    Right operand.
 * @return DCG_OK.
 */
static inline int c_dcg_node_expr_apply_eq(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b) {
    (void) c_dcg_var_init(out);

    dcg_var_numeric left  = c_dcg_var_numeric_of(a);
    dcg_var_numeric right = c_dcg_var_numeric_of(b);
    bool            equal;

    if (left != VAR_NUMERIC_NONE && right != VAR_NUMERIC_NONE) {
        equal = (left == VAR_NUMERIC_DOUBLE || right == VAR_NUMERIC_DOUBLE) ? c_dcg_var_as_double(a) == c_dcg_var_as_double(b) : c_dcg_var_as_int(a) == c_dcg_var_as_int(b);
    }
    else {
        equal = left == right && c_dcg_var_equals(a, b);
    }
    return c_dcg_var_init_bool(out, equal);
}

/**
 * @brief Inequality: the negation of what c_dcg_node_expr_apply_eq() holds.
 *
 * @param out  Receives the value.
 * @param a    Left operand.
 * @param b    Right operand.
 * @return DCG_OK.
 */
static inline int c_dcg_node_expr_apply_ne(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b) {
    (void) c_dcg_var_init(out);

    dcg_var_numeric left  = c_dcg_var_numeric_of(a);
    dcg_var_numeric right = c_dcg_var_numeric_of(b);
    bool            equal;

    if (left != VAR_NUMERIC_NONE && right != VAR_NUMERIC_NONE) {
        equal = (left == VAR_NUMERIC_DOUBLE || right == VAR_NUMERIC_DOUBLE) ? c_dcg_var_as_double(a) == c_dcg_var_as_double(b) : c_dcg_var_as_int(a) == c_dcg_var_as_int(b);
    }
    else {
        equal = left == right && c_dcg_var_equals(a, b);
    }
    return c_dcg_var_init_bool(out, !equal);
}

/**
 * @brief The ordering of two operands: numbers by value, strings by text, and
 * anything else refused.
 *
 * The four ordering operators differ only in the comparison they make of this,
 * which is why they read it here.
 *
 * @param out  Receives -1, 0 or 1 as `a` orders before, with, or after `b`.
 * @param a    Left operand.
 * @param b    Right operand.
 * @return DCG_OK, or DCG_ERR_MATH for operands with no order.
 */
static inline int c_dcg_node_expr_order(int* out, const dcg_var_t* a, const dcg_var_t* b) {
    if (c_dcg_var_numeric_of(a) != VAR_NUMERIC_NONE && c_dcg_var_numeric_of(b) != VAR_NUMERIC_NONE) {
        double x = c_dcg_var_as_double(a);
        double y = c_dcg_var_as_double(b);
        *out     = x < y ? -1 : (x > y ? 1 : 0);
        return DCG_OK;
    }
    if (c_dcg_var_ref_base(a->dtype) == VAR_TYPE_STRING && c_dcg_var_ref_base(b->dtype) == VAR_TYPE_STRING) {
        const char* x = c_dcg_var_as_string(a);
        const char* y = c_dcg_var_as_string(b);
        *out          = strcmp(x ? x : "", y ? y : "");
        return DCG_OK;
    }
    return DCG_ERR_MATH;
}

/**
 * @brief Greater than, by the order c_dcg_node_expr_order() reads.
 *
 * @param out  Receives the value.
 * @param a    Left operand.
 * @param b    Right operand.
 * @return DCG_OK, or DCG_ERR_MATH for operands with no order.
 */
static inline int c_dcg_node_expr_apply_gt(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b) {
    (void) c_dcg_var_init(out);
    int order    = 0;
    int ret_code = c_dcg_node_expr_order(&order, a, b);
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_var_init_bool(out, order > 0);
}

/**
 * @brief Greater than or equal, by the order c_dcg_node_expr_order() reads.
 *
 * @param out  Receives the value.
 * @param a    Left operand.
 * @param b    Right operand.
 * @return DCG_OK, or DCG_ERR_MATH for operands with no order.
 */
static inline int c_dcg_node_expr_apply_ge(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b) {
    (void) c_dcg_var_init(out);
    int order    = 0;
    int ret_code = c_dcg_node_expr_order(&order, a, b);
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_var_init_bool(out, order >= 0);
}

/**
 * @brief Less than, by the order c_dcg_node_expr_order() reads.
 *
 * @param out  Receives the value.
 * @param a    Left operand.
 * @param b    Right operand.
 * @return DCG_OK, or DCG_ERR_MATH for operands with no order.
 */
static inline int c_dcg_node_expr_apply_lt(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b) {
    (void) c_dcg_var_init(out);
    int order    = 0;
    int ret_code = c_dcg_node_expr_order(&order, a, b);
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_var_init_bool(out, order < 0);
}

/**
 * @brief Less than or equal, by the order c_dcg_node_expr_order() reads.
 *
 * @param out  Receives the value.
 * @param a    Left operand.
 * @param b    Right operand.
 * @return DCG_OK, or DCG_ERR_MATH for operands with no order.
 */
static inline int c_dcg_node_expr_apply_le(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b) {
    (void) c_dcg_var_init(out);
    int order    = 0;
    int ret_code = c_dcg_node_expr_order(&order, a, b);
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_var_init_bool(out, order <= 0);
}

/**
 * @brief Logical and: an OPERAND rather than a bool.
 *
 * Python's `and` answers with one of its operands, and so does this: the left one
 * decides, and the right one is the answer only when the left one did not.
 *
 * @param out  Receives the value.
 * @param a    Left operand.
 * @param b    Right operand.
 * @return DCG_OK.
 */
static inline int c_dcg_node_expr_apply_and(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b) {
    (void) c_dcg_var_init(out);
    *out = c_dcg_var_is_truthy(a) ? *b : *a;
    return DCG_OK;
}

/**
 * @brief Logical or: the other operand, chosen the other way round.
 *
 * @param out  Receives the value.
 * @param a    Left operand.
 * @param b    Right operand.
 * @return DCG_OK.
 */
static inline int c_dcg_node_expr_apply_or(dcg_var_t* out, const dcg_var_t* a, const dcg_var_t* b) {
    (void) c_dcg_var_init(out);
    *out = c_dcg_var_is_truthy(a) ? *a : *b;
    return DCG_OK;
}

// ========== The Rules, One Per Operator ==========

/*
 * What this family's nodes evaluate to: the operator applied to the operands the
 * node binds. The rules live HERE, with the operator and the operands they are
 * about, ONE PER OPERATOR - and the rule is the node's own.
 *
 * A rule BEGINS by producing its own operands: the components are the node's own
 * fields, so the rule runs them and writes what they are worth into the
 * workspace itself (c_dcg_node_expr_eval_operands), and only then applies its
 * kernel to the slots. So a rule is a pass-through - operands produced, one
 * kernel, the value into the node's own out slot - with nothing to check, nothing
 * to copy and nothing to follow: what an operand refers to has been settled by
 * the time the kernel sees it.
 *
 * WHICH rule runs is settled once, from the flat list (DCG_EXPR_EVAL_FNS), when
 * the node's operator is set - and where that answer is KEPT is what
 * DCG_EVAL_DIRECT_HOOKS decides: injected onto the node as its type-eval rule
 * (dcg_node_eval_ctx.type_eval_fn), so an evaluation is a call through that
 * pointer and nothing else; or left to be found again at evaluation time by
 * c_dcg_node_expr_eval, which asks the same two questions. One rule, reached two
 * ways - so the two builds cannot compute different values.
 */

/**
 * @brief The rule of a node that has no operator to apply: saying so is the rule.
 *
 * What an operator node is born with (see c_dcg_node_expr_eval_of_arity), and
 * what an operator of another arity leaves standing - a comparison handed to a
 * unary node, a family head, an operator code no rule exists for (see
 * c_dcg_node_expr_set_op). Refusing is the whole of it: no operand is read and
 * the node's slot is left as it was.
 *
 * @param node       The operator node (unused).
 * @param user_data  Unused: a node's own rule carries no context.
 * @return DCG_ERR_TYPE.
 */
static inline int c_dcg_node_expr_eval_refuse(dcg_node* node, void* user_data) {
    (void) node;
    (void) user_data;
    return DCG_ERR_TYPE;
}

/**
 * @brief The rule of an operator node of no arity: saying so is the rule.
 *
 * Reached by a node built with the operator family's head type (DCG_NODE_OP)
 * rather than one of the four arities - a node nobody can evaluate, which
 * reports rather than guessing at an arity.
 *
 * @param node       The operator node.
 * @param user_data  Unused.
 * @return DCG_ERR_TYPE.
 */
static inline int c_dcg_node_expr_eval_unknown(dcg_node* node, void* user_data) {
    (void) user_data;

    (void) fprintf(stderr, "c_dcg_node_expr_eval_unknown: no arity to evaluate operator node type %s (0x%04x) at %p by\n", c_dcg_node_type_name(node->ntype), (unsigned) node->ntype, (const void*) node);
    return DCG_ERR_TYPE;
}

/**
 * @brief The rule of an if-expression: the arm its condition picks.
 *
 * The condition is read FIRST, and then only the arm it selects, so an operand
 * that cannot produce a value on the side not taken is not this node's problem.
 *
 * @param node       The ternary node (a dcg_expression_node).
 * @param user_data  Unused: a node's own rule carries no context.
 * @return DCG_OK, or the code that stopped it.
 */
static inline int c_dcg_node_expr_eval_ternary(dcg_node* node, void* user_data) {
    (void) user_data;

    dcg_expression_node* expr = (dcg_expression_node*) node;

    /* The condition FIRST, and then only the arm it picks: an operand on the side
     * not taken is not this node's to produce. */
    int                  ret_code = c_dcg_node_expr_eval_operand(expr, 0);
    if (ret_code != DCG_OK) return ret_code;

    size_t arm = c_dcg_var_is_truthy(&expr->args[0]) ? 1 : 2;
    ret_code   = c_dcg_node_expr_eval_operand(expr, arm);
    if (ret_code != DCG_OK) return ret_code;

    node->out = expr->args[arm];
    return DCG_OK;
}

/**
 * @brief The rule of a call: there is none, and saying so is the rule.
 *
 * A call's callee is composed into its display text and stored nowhere, so
 * there is nothing to apply; it reports rather than producing a value nothing
 * can read.
 *
 * @param node       The call node.
 * @param user_data  Unused.
 * @return DCG_ERR_TYPE.
 */
static inline int c_dcg_node_expr_eval_call(dcg_node* node, void* user_data) {
    (void) user_data;

    (void) fprintf(stderr, "c_dcg_node_expr_eval_call: a call node at %p has no evaluation: its callee is composed into its repr and stored nowhere\n", (const void*) node);
    return DCG_ERR_TYPE;
}

/**
 * @brief `-a`: the node's one operand, negated (c_dcg_node_expr_apply_neg).
 *
 * @param node       The operator node (a dcg_expression_node).
 * @param user_data  Unused: a node's own rule carries no context.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_expr_eval_neg(dcg_node* node, void* user_data) {
    (void) user_data;

    dcg_expression_node* expr     = (dcg_expression_node*) node;
    int                  ret_code = c_dcg_node_expr_eval_operands(expr); /* this node's own operands, first */
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_node_expr_apply_neg(&node->out, &expr->args[0], NULL);
}

/**
 * @brief `not a`: the node's one operand, negated as a truth (c_dcg_node_expr_apply_not).
 *
 * @param node       The operator node (a dcg_expression_node).
 * @param user_data  Unused: a node's own rule carries no context.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_expr_eval_not(dcg_node* node, void* user_data) {
    (void) user_data;

    dcg_expression_node* expr     = (dcg_expression_node*) node;
    int                  ret_code = c_dcg_node_expr_eval_operands(expr); /* this node's own operands, first */
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_node_expr_apply_not(&node->out, &expr->args[0], NULL);
}

/**
 * @brief `a + b`: the node's two operands, added (c_dcg_node_expr_apply_add).
 *
 * @param node       The operator node (a dcg_expression_node).
 * @param user_data  Unused: a node's own rule carries no context.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_expr_eval_add(dcg_node* node, void* user_data) {
    (void) user_data;

    dcg_expression_node* expr     = (dcg_expression_node*) node;
    int                  ret_code = c_dcg_node_expr_eval_operands(expr); /* this node's own operands, first */
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_node_expr_apply_add(&node->out, &expr->args[0], &expr->args[1]);
}

/**
 * @brief `a - b`: the node's two operands, subtracted (c_dcg_node_expr_apply_sub).
 *
 * @param node       The operator node (a dcg_expression_node).
 * @param user_data  Unused: a node's own rule carries no context.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_expr_eval_sub(dcg_node* node, void* user_data) {
    (void) user_data;

    dcg_expression_node* expr     = (dcg_expression_node*) node;
    int                  ret_code = c_dcg_node_expr_eval_operands(expr); /* this node's own operands, first */
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_node_expr_apply_sub(&node->out, &expr->args[0], &expr->args[1]);
}

/**
 * @brief `a * b`: the node's two operands, multiplied (c_dcg_node_expr_apply_mul).
 *
 * @param node       The operator node (a dcg_expression_node).
 * @param user_data  Unused: a node's own rule carries no context.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_expr_eval_mul(dcg_node* node, void* user_data) {
    (void) user_data;

    dcg_expression_node* expr     = (dcg_expression_node*) node;
    int                  ret_code = c_dcg_node_expr_eval_operands(expr); /* this node's own operands, first */
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_node_expr_apply_mul(&node->out, &expr->args[0], &expr->args[1]);
}

/**
 * @brief `a / b`: the node's two operands, divided (c_dcg_node_expr_apply_div).
 *
 * @param node       The operator node (a dcg_expression_node).
 * @param user_data  Unused: a node's own rule carries no context.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_expr_eval_div(dcg_node* node, void* user_data) {
    (void) user_data;

    dcg_expression_node* expr     = (dcg_expression_node*) node;
    int                  ret_code = c_dcg_node_expr_eval_operands(expr); /* this node's own operands, first */
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_node_expr_apply_div(&node->out, &expr->args[0], &expr->args[1]);
}

/**
 * @brief `a // b`: the node's two operands, floor-divided (c_dcg_node_expr_apply_floordiv).
 *
 * @param node       The operator node (a dcg_expression_node).
 * @param user_data  Unused: a node's own rule carries no context.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_expr_eval_floordiv(dcg_node* node, void* user_data) {
    (void) user_data;

    dcg_expression_node* expr     = (dcg_expression_node*) node;
    int                  ret_code = c_dcg_node_expr_eval_operands(expr); /* this node's own operands, first */
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_node_expr_apply_floordiv(&node->out, &expr->args[0], &expr->args[1]);
}

/**
 * @brief `a ** b`: the node's two operands, raised (c_dcg_node_expr_apply_pow).
 *
 * @param node       The operator node (a dcg_expression_node).
 * @param user_data  Unused: a node's own rule carries no context.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_expr_eval_pow(dcg_node* node, void* user_data) {
    (void) user_data;

    dcg_expression_node* expr     = (dcg_expression_node*) node;
    int                  ret_code = c_dcg_node_expr_eval_operands(expr); /* this node's own operands, first */
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_node_expr_apply_pow(&node->out, &expr->args[0], &expr->args[1]);
}

/**
 * @brief `a == b`: the node's two operands, compared (c_dcg_node_expr_apply_eq).
 *
 * @param node       The operator node (a dcg_expression_node).
 * @param user_data  Unused: a node's own rule carries no context.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_expr_eval_eq(dcg_node* node, void* user_data) {
    (void) user_data;

    dcg_expression_node* expr     = (dcg_expression_node*) node;
    int                  ret_code = c_dcg_node_expr_eval_operands(expr); /* this node's own operands, first */
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_node_expr_apply_eq(&node->out, &expr->args[0], &expr->args[1]);
}

/**
 * @brief `a != b`: the node's two operands, compared (c_dcg_node_expr_apply_ne).
 *
 * @param node       The operator node (a dcg_expression_node).
 * @param user_data  Unused: a node's own rule carries no context.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_expr_eval_ne(dcg_node* node, void* user_data) {
    (void) user_data;

    dcg_expression_node* expr     = (dcg_expression_node*) node;
    int                  ret_code = c_dcg_node_expr_eval_operands(expr); /* this node's own operands, first */
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_node_expr_apply_ne(&node->out, &expr->args[0], &expr->args[1]);
}

/**
 * @brief `a > b`: the node's two operands, ordered (c_dcg_node_expr_apply_gt).
 *
 * @param node       The operator node (a dcg_expression_node).
 * @param user_data  Unused: a node's own rule carries no context.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_expr_eval_gt(dcg_node* node, void* user_data) {
    (void) user_data;

    dcg_expression_node* expr     = (dcg_expression_node*) node;
    int                  ret_code = c_dcg_node_expr_eval_operands(expr); /* this node's own operands, first */
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_node_expr_apply_gt(&node->out, &expr->args[0], &expr->args[1]);
}

/**
 * @brief `a >= b`: the node's two operands, ordered (c_dcg_node_expr_apply_ge).
 *
 * @param node       The operator node (a dcg_expression_node).
 * @param user_data  Unused: a node's own rule carries no context.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_expr_eval_ge(dcg_node* node, void* user_data) {
    (void) user_data;

    dcg_expression_node* expr     = (dcg_expression_node*) node;
    int                  ret_code = c_dcg_node_expr_eval_operands(expr); /* this node's own operands, first */
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_node_expr_apply_ge(&node->out, &expr->args[0], &expr->args[1]);
}

/**
 * @brief `a < b`: the node's two operands, ordered (c_dcg_node_expr_apply_lt).
 *
 * @param node       The operator node (a dcg_expression_node).
 * @param user_data  Unused: a node's own rule carries no context.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_expr_eval_lt(dcg_node* node, void* user_data) {
    (void) user_data;

    dcg_expression_node* expr     = (dcg_expression_node*) node;
    int                  ret_code = c_dcg_node_expr_eval_operands(expr); /* this node's own operands, first */
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_node_expr_apply_lt(&node->out, &expr->args[0], &expr->args[1]);
}

/**
 * @brief `a <= b`: the node's two operands, ordered (c_dcg_node_expr_apply_le).
 *
 * @param node       The operator node (a dcg_expression_node).
 * @param user_data  Unused: a node's own rule carries no context.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_expr_eval_le(dcg_node* node, void* user_data) {
    (void) user_data;

    dcg_expression_node* expr     = (dcg_expression_node*) node;
    int                  ret_code = c_dcg_node_expr_eval_operands(expr); /* this node's own operands, first */
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_node_expr_apply_le(&node->out, &expr->args[0], &expr->args[1]);
}

/**
 * @brief `a and b`: the node's two operands, the operand one of them picks
 * (c_dcg_node_expr_apply_and).
 *
 * @param node       The operator node (a dcg_expression_node).
 * @param user_data  Unused: a node's own rule carries no context.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_expr_eval_and(dcg_node* node, void* user_data) {
    (void) user_data;

    dcg_expression_node* expr     = (dcg_expression_node*) node;
    int                  ret_code = c_dcg_node_expr_eval_operands(expr); /* this node's own operands, first */
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_node_expr_apply_and(&node->out, &expr->args[0], &expr->args[1]);
}

/**
 * @brief `a or b`: the node's two operands, the operand one of them picks
 * (c_dcg_node_expr_apply_or).
 *
 * @param node       The operator node (a dcg_expression_node).
 * @param user_data  Unused: a node's own rule carries no context.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_expr_eval_or(dcg_node* node, void* user_data) {
    (void) user_data;

    dcg_expression_node* expr     = (dcg_expression_node*) node;
    int                  ret_code = c_dcg_node_expr_eval_operands(expr); /* this node's own operands, first */
    if (ret_code != DCG_OK) return ret_code;
    return c_dcg_node_expr_apply_or(&node->out, &expr->args[0], &expr->args[1]);
}

// ========== The Flat List of Rules ==========

/**
 * @brief Where an operator's rule sits in the flat list of type-eval rules.
 *
 * The operator code carries its own two dimensions - the family nibble and the
 * variant number - and the list is those two flattened into one: the family's
 * slots first, the variant within them. A rule is therefore found by ARITHMETIC
 * on the code, never by a search of it.
 *
 * @param op  Operator code.
 * @return The operator's slot in DCG_EXPR_EVAL_FNS.
 */
static inline size_t c_dcg_op_code_index(dcg_op_code op) {
    return (size_t) (((((int) op & DCG_OP_FAMILY_MASK) >> 8) * DCG_OP_FAMILY_SLOTS) + ((int) op & DCG_OP_VARIANT_MASK));
}

/**
 * @brief The flat list of the family's type-eval rules - one per operator.
 *
 * THE LIST THE ONE-TIME CHOICE READS (c_dcg_node_expr_set_op): a node's rule is
 * its operator's entry here, taken once, when the operator is set, and kept on
 * the node from then on. Evaluating the node is a call through that pointer, so
 * nothing searches this list at run time.
 *
 * The slots follow the operator codes themselves - the family's slots first,
 * then the variant number within the family - so the index is arithmetic on the
 * code (c_dcg_op_code_index) and a reader can see where each rule sits. A slot
 * that names no operator with a rule of its own holds the rule that says so,
 * which is what lets a node be given ANY code and still evaluate to an answer
 * rather than to a crash.
 *
 * The rules that belong to an ARITY rather than to an operator - the
 * if-expression and the call - are not here: they are chosen by the node's type
 * as it is built (c_dcg_node_expr_eval_of_arity), because those nodes carry no
 * operator to be indexed with.
 */
static const dcg_node_hook_fn DCG_EXPR_EVAL_FNS[DCG_OP_FAMILIES * DCG_OP_FAMILY_SLOTS] = {
    /* 0x00__: no operator */
    c_dcg_node_expr_eval_unknown,  // 0x0000 DCG_OP_NONE
    c_dcg_node_expr_eval_unknown,  // 0x0001
    c_dcg_node_expr_eval_unknown,  // 0x0002
    c_dcg_node_expr_eval_unknown,  // 0x0003
    c_dcg_node_expr_eval_unknown,  // 0x0004
    c_dcg_node_expr_eval_unknown,  // 0x0005
    c_dcg_node_expr_eval_unknown,  // 0x0006
    c_dcg_node_expr_eval_unknown,  // 0x0007
    /* 0x01__: arithmetic - the head, then six binary operators and one unary */
    c_dcg_node_expr_eval_unknown,   // 0x0100 DCG_OP_ARITH (the family head)
    c_dcg_node_expr_eval_add,       // 0x0101 DCG_OP_ADD
    c_dcg_node_expr_eval_sub,       // 0x0102 DCG_OP_SUB
    c_dcg_node_expr_eval_mul,       // 0x0103 DCG_OP_MUL
    c_dcg_node_expr_eval_div,       // 0x0104 DCG_OP_DIV
    c_dcg_node_expr_eval_floordiv,  // 0x0105 DCG_OP_FLOORDIV
    c_dcg_node_expr_eval_pow,       // 0x0106 DCG_OP_POW
    c_dcg_node_expr_eval_neg,       // 0x0107 DCG_OP_NEG (unary)
    /* 0x02__: comparison */
    c_dcg_node_expr_eval_unknown,  // 0x0200 DCG_OP_COMPARE (the family head)
    c_dcg_node_expr_eval_eq,       // 0x0201 DCG_OP_EQ
    c_dcg_node_expr_eval_ne,       // 0x0202 DCG_OP_NE
    c_dcg_node_expr_eval_gt,       // 0x0203 DCG_OP_GT
    c_dcg_node_expr_eval_ge,       // 0x0204 DCG_OP_GE
    c_dcg_node_expr_eval_lt,       // 0x0205 DCG_OP_LT
    c_dcg_node_expr_eval_le,       // 0x0206 DCG_OP_LE
    c_dcg_node_expr_eval_unknown,  // 0x0207
    /* 0x03__: logical */
    c_dcg_node_expr_eval_unknown,  // 0x0300 DCG_OP_LOGIC (the family head)
    c_dcg_node_expr_eval_and,      // 0x0301 DCG_OP_AND
    c_dcg_node_expr_eval_or,       // 0x0302 DCG_OP_OR
    c_dcg_node_expr_eval_not,      // 0x0303 DCG_OP_NOT (unary)
    c_dcg_node_expr_eval_unknown,  // 0x0304
    c_dcg_node_expr_eval_unknown,  // 0x0305
    c_dcg_node_expr_eval_unknown,  // 0x0306
    c_dcg_node_expr_eval_unknown,  // 0x0307
    /* 0x04__: access - an attribute or an item read is not computed here, so its
     * slots refuse rather than evaluate */
    c_dcg_node_expr_eval_unknown,  // 0x0400 DCG_OP_ACCESS (the family head)
    c_dcg_node_expr_eval_refuse,   // 0x0401 DCG_OP_ATTR
    c_dcg_node_expr_eval_refuse,   // 0x0402 DCG_OP_GETITEM
    c_dcg_node_expr_eval_unknown,  // 0x0403
    c_dcg_node_expr_eval_unknown,  // 0x0404
    c_dcg_node_expr_eval_unknown,  // 0x0405
    c_dcg_node_expr_eval_unknown,  // 0x0406
    c_dcg_node_expr_eval_unknown   // 0x0407
};

/**
 * @brief The rule a node is born with: its arity's own.
 *
 * Every operator node is built here (c_dcg_node_new_expr), before any operator
 * is set: an operator node is given the refusing rule until its operator
 * arrives, and a node whose arity has no operator to apply - the if-expression,
 * the call - is given the rule that IS its evaluation. Those two cannot come
 * from the flat list: an if-expression and a call carry no operator for the list
 * to be indexed with, so their rule is the arity's, chosen once, here.
 *
 * @param ntype  The node's type.
 * @return The rule to install - the refusing one for a node awaiting an
 *         operator, and the reporting one for a type this family has no arity
 *         to evaluate.
 */
static inline dcg_node_hook_fn c_dcg_node_expr_eval_of_arity(dcg_node_type ntype) {
    switch (ntype) {
        case DCG_NODE_TERNARY:
            return c_dcg_node_expr_eval_ternary;
        case DCG_NODE_CALL:
            return c_dcg_node_expr_eval_call;
        case DCG_NODE_UNARY:
        case DCG_NODE_BINARY:
            return c_dcg_node_expr_eval_refuse;
        default:
            return c_dcg_node_expr_eval_unknown;
    }
}

/**
 * @brief The family's evaluation of a node that carries no rule of its own: the
 * rule found from the code - the DISPATCH.
 *
 * What a build with DCG_EVAL_DIRECT_HOOKS OFF reaches, through the protocol's
 * dispatch (c_dcg_node_eval_default): those nodes were taught nothing as they
 * were built, so the rule is looked up here instead. It finds the same rule the
 * injected build keeps ON the node - one rule, reached two ways, so the two
 * builds cannot compute different values - by the same two questions the
 * injection asks once: the operator, when it belongs to this node's arity, and
 * the arity itself when it does not (an if-expression and a call carry no
 * operator to look one up with).
 *
 * @param node       Expression node to evaluate.
 * @param user_data  Unused: the rule it finds carries no context.
 * @return DCG_OK, or a DCG_ERR_* code.
 */
static inline int c_dcg_node_expr_eval(dcg_node* node, void* user_data) {
    dcg_expression_node* expr = (dcg_expression_node*) node;
    if (c_dcg_op_code_node_type(expr->op) == node->ntype) return DCG_EXPR_EVAL_FNS[c_dcg_op_code_index(expr->op)](node, user_data);
    return c_dcg_node_expr_eval_of_arity(node->ntype)(node, user_data);
}

/**
 * @brief Which kernel applies an operator, in one arity - the APPLY-SIDE lookup.
 *
 * For a caller that has an operator CODE and no node (c_dcg_node_expr_apply_unary
 * / c_dcg_node_expr_apply_binary). A node is not this function's business any
 * more: which rule EVALUATES a node was settled when its operator was set, and
 * the rule reads the kernel it needs directly.
 *
 * An op this arity has no kernel for - a comparison applied as a unary, AND as a
 * unary, the access ops, a family mask - answers with
 * c_dcg_node_expr_apply_refuse(), so a caller never has to ask whether it has an
 * answer. A TERNARY or a CALL has no operator to apply at all and answers NULL:
 * the ternary reads the arm its condition picks, and a call's callee is composed
 * into its display text and stored nowhere.
 *
 * @param ntype  The arity whose kernels to search.
 * @param op     The operator code.
 * @return The kernel for (arity, op), or NULL for an arity with no operator.
 */
static inline dcg_expr_apply_fn c_dcg_node_expr_apply_fn_of(dcg_node_type ntype, dcg_op_code op) {
    if (ntype == DCG_NODE_UNARY) {
        switch (op) {
            case DCG_OP_NEG:
                return c_dcg_node_expr_apply_neg;
            case DCG_OP_NOT:
                return c_dcg_node_expr_apply_not;
            default:
                return c_dcg_node_expr_apply_refuse;
        }
    }
    if (ntype == DCG_NODE_BINARY) {
        switch (op) {
            case DCG_OP_ADD:
                return c_dcg_node_expr_apply_add;
            case DCG_OP_SUB:
                return c_dcg_node_expr_apply_sub;
            case DCG_OP_MUL:
                return c_dcg_node_expr_apply_mul;
            case DCG_OP_DIV:
                return c_dcg_node_expr_apply_div;
            case DCG_OP_FLOORDIV:
                return c_dcg_node_expr_apply_floordiv;
            case DCG_OP_POW:
                return c_dcg_node_expr_apply_pow;
            case DCG_OP_EQ:
                return c_dcg_node_expr_apply_eq;
            case DCG_OP_NE:
                return c_dcg_node_expr_apply_ne;
            case DCG_OP_GT:
                return c_dcg_node_expr_apply_gt;
            case DCG_OP_GE:
                return c_dcg_node_expr_apply_ge;
            case DCG_OP_LT:
                return c_dcg_node_expr_apply_lt;
            case DCG_OP_LE:
                return c_dcg_node_expr_apply_le;
            case DCG_OP_AND:
                return c_dcg_node_expr_apply_and;
            case DCG_OP_OR:
                return c_dcg_node_expr_apply_or;
            default:
                return c_dcg_node_expr_apply_refuse;
        }
    }
    return NULL;
}

/**
 * @brief Set a node's operator, and - when the build injects - the rule that
 * evaluates it.
 *
 * The operator is known when the node is built - the constructors take the code
 * - so the ONE-TIME choice is here, out of the flat list (DCG_EXPR_EVAL_FNS).
 * With DCG_EVAL_DIRECT_HOOKS on, the rule is installed on the node as its
 * type-eval rule (dcg_node_eval_ctx.type_eval_fn) and every evaluation after that
 * is a call through that pointer; with it off, nothing is installed and the rule
 * is found again at evaluation time (c_dcg_node_expr_eval) - the same rule, the
 * same two questions, asked at the other end.
 *
 * Only an operator that BELONGS to the node's arity installs anything. A node
 * whose arity has no operator to apply keeps the rule it was born with - the
 * if-expression, the call - and an operator of another arity (a comparison
 * handed to a unary node, a family head, a code with no rule at all) leaves the
 * refusing rule standing. The two never disagree, because the code and the rule
 * are written together here and nowhere else.
 *
 * @param node  Expression node to set it on.
 * @param op    Operator code.
 * @return DCG_OK, or DCG_ERR_INVALID_ARG.
 */
static inline int c_dcg_node_expr_set_op(dcg_expression_node* node, dcg_op_code op) {
    if (!node) return DCG_ERR_INVALID_ARG;

    node->op = op;
#if DCG_EVAL_DIRECT_HOOKS
    if (c_dcg_op_code_node_type(op) == node->base.ntype) node->base.eval_ctx.type_eval_fn = DCG_EXPR_EVAL_FNS[c_dcg_op_code_index(op)];
#endif
    return DCG_OK;
}

/**
 * @brief The value of a unary operator over one operand - by operator code.
 *
 * The runtime dispatch, for a caller that has a code and no node: the arity's
 * kernel for the operator is looked up and called, so the answer is the one the
 * operator's own evaluation reaches - c_dcg_node_expr_eval_neg or
 * c_dcg_node_expr_eval_not read their operand and call the same kernel.
 *
 * @param out  Receives the value.
 * @param op   Operator (DCG_OP_NEG, DCG_OP_NOT).
 * @param a    The operand.
 * @return DCG_OK, DCG_ERR_TYPE (an operator the unary arity has no meaning for)
 *         or DCG_ERR_MATH when the operand is outside the operator's domain.
 */
static inline int c_dcg_node_expr_apply_unary(dcg_var_t* out, dcg_op_code op, const dcg_var_t* a) {
    return c_dcg_node_expr_apply_fn_of(DCG_NODE_UNARY, op)(out, a, NULL);
}

/**
 * @brief The value of a binary operator over two operands - by operator code.
 *
 * The runtime dispatch, for a caller that has a code and no node: the arity's
 * kernel for the operator is looked up and called, so the answer is the one the
 * operator's own evaluation reaches - c_dcg_node_expr_eval_add and its siblings
 * read their two operands and call the same kernel.
 *
 * @param out  Receives the value.
 * @param op   Operator.
 * @param a    Left operand.
 * @param b    Right operand.
 * @return DCG_OK, DCG_ERR_TYPE (an operator this arity has no kernel for) or
 *         DCG_ERR_MATH.
 */
static inline int c_dcg_node_expr_apply_binary(dcg_var_t* out, dcg_op_code op, const dcg_var_t* a, const dcg_var_t* b) {
    return c_dcg_node_expr_apply_fn_of(DCG_NODE_BINARY, op)(out, a, b);
}

// ========== The Workspace, at Run Time ==========

/*
 * The slot array is a WORKSPACE: an operand's value is produced by RUNNING the
 * node it was bound to, and what comes back is written into the slot as a
 * LITERAL - a reference is read through here, once, so the rule that runs
 * afterwards finds plain values and never has to ask where an operand came from
 * or follow anything.
 *
 * The loop is the FAMILY's, over its own fields: `components` and `args` are the
 * expression node's, so producing one from the other is this header's business,
 * and the rule that needs its operands asks for them itself. What running a node
 * MEANS is still the protocol's - the entry is c_dcg_node_eval, declared above -
 * and the walk knows nothing about operands at all.
 *
 * ONE SLOT AT A TIME, and that is the contract's shape rather than a convenience:
 * a component is run and its slot filled before the next one is touched, because
 * two operands may share storage and what one evaluation writes may be what the
 * other reads.
 */

/**
 * @brief Run ONE component and write what it is worth into its operand slot.
 *
 * A slot that was never bound has no component to run and is left as it stands:
 * the node has nothing to read there, which is the state
 * c_dcg_node_expr_bind() left it in.
 *
 * @param node   Expression node whose operand to produce.
 * @param index  Which operand.
 * @return DCG_OK, or the code the component's own evaluation stopped with.
 */
static inline int c_dcg_node_expr_eval_operand(dcg_expression_node* node, size_t index) {
    dcg_node* component = node->components[index];
    if (!component) return DCG_OK;

    int ret_code = c_dcg_node_eval(component, true);
    if (ret_code != DCG_OK) return ret_code;

    c_dcg_var_snapshot(&node->args[index], &component->out);
    return DCG_OK;
}

/**
 * @brief Run every component of a node, and fill the workspace with their values.
 *
 * What an operator rule calls before it applies anything: after this, `args[i]`
 * holds what `components[i]` is worth NOW, fully dereferenced.
 *
 * @param node  Expression node whose operands to produce.
 * @return DCG_OK, or the first failure a component raised.
 */
static inline int c_dcg_node_expr_eval_operands(dcg_expression_node* node) {
    for (size_t i = 0; i < node->n_args; i++) {
        int ret_code = c_dcg_node_expr_eval_operand(node, i);
        if (ret_code != DCG_OK) return ret_code;
    }
    return DCG_OK;
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

    (void) c_dcg_node_expr_set_op(node, op);
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

    (void) c_dcg_node_expr_set_op(node, op);
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

    (void) c_dcg_node_expr_set_op(node, op);
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

    (void) c_dcg_node_expr_set_op(node, op);
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
 * What is written here is the graph AS BUILT, and an evaluation overwrites it:
 * before the operator runs, the node's own rule re-fills every slot with the
 * value of the moment (see c_dcg_node_expr_eval_operands). So a populated slot is
 * a starting point for reading the graph without running it - which is what
 * static analysis and the bake pass want - and never a promise about what an
 * evaluation will use. The components array is the record that stays true.
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
    }
    if (ret_code != DCG_OK) return ret_code;

    /* The hold goes on last, so a slot that could not be filled leaves the
     * previous operand standing. It goes on EVERY operand, whatever the slot
     * took: a slot filled with a value is self-contained as far as reading goes,
     * but the operand is still what the slot was made from, and an expression is
     * read back by whoever built it - a debugger, a renderer, a second bind -
     * through exactly this array. One rule, one reference count, no case that
     * has to be reasoned about twice.
     *
     * The new reference is taken before the old one is given back: rebinding a
     * slot to the operand it already holds, or two expressions trading one, must
     * not drop the last reference between the two steps. */
    dcg_node* previous = node->components[index];
    if (previous == input) return DCG_OK;

    c_ap_incref(input);
    if (previous) c_ap_decref(previous);
    node->components[index] = input;
    return DCG_OK;
}

#endif  // C_DCG_BAKE_EXPR_H
