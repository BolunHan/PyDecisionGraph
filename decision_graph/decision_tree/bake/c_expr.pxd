from cpython.object cimport PyObject

from cbase.allocator_protocol.c_allocator_protocol cimport allocator_protocol

from .c_node cimport LogicNode, dcg_node, dcg_node_type
from .c_var cimport dcg_var_t


cdef extern from "decision_graph/decision_tree/bake/c_expr.h":
    const size_t DCG_EXPR_DEFAULT_ARGS

    ctypedef enum dcg_op_code:
        DCG_OP_NONE
        DCG_OP_ARITH
        DCG_OP_ADD
        DCG_OP_SUB
        DCG_OP_MUL
        DCG_OP_DIV
        DCG_OP_FLOORDIV
        DCG_OP_POW
        DCG_OP_NEG
        DCG_OP_COMPARE
        DCG_OP_EQ
        DCG_OP_NE
        DCG_OP_GT
        DCG_OP_GE
        DCG_OP_LT
        DCG_OP_LE
        DCG_OP_LOGIC
        DCG_OP_AND
        DCG_OP_OR
        DCG_OP_NOT
        DCG_OP_ACCESS
        DCG_OP_ATTR
        DCG_OP_GETITEM

    ctypedef enum dcg_op_mask:
        DCG_OP_FAMILY_MASK
        DCG_OP_VARIANT_MASK

    ctypedef struct dcg_expression_node:
        dcg_node base
        dcg_op_code op
        size_t n_args
        dcg_node** components
        dcg_var_t args[]

    dcg_expression_node* c_dcg_node_new_expr(size_t n_args, dcg_node_type ntype, allocator_protocol* allocator) noexcept nogil
    void c_dcg_node_free_expr(dcg_expression_node* node) noexcept nogil
    dcg_expression_node* c_dcg_node_new_expr_unary(dcg_op_code op, dcg_node* src, allocator_protocol* allocator) noexcept nogil
    dcg_expression_node* c_dcg_node_new_expr_binary(dcg_op_code op, dcg_node* var_0, dcg_node* var_1, allocator_protocol* allocator) noexcept nogil
    dcg_expression_node* c_dcg_node_new_expr_ternary(dcg_op_code op, dcg_node* var_0, dcg_node* var_1, dcg_node* var_2, allocator_protocol* allocator) noexcept nogil
    dcg_expression_node* c_dcg_node_new_expr_call(dcg_op_code op, dcg_node** vars, size_t n_vars, const char* repr, allocator_protocol* allocator) noexcept nogil
    int c_dcg_node_expr_bind(dcg_expression_node* node, size_t index, dcg_node* input) noexcept nogil
    int c_dcg_node_expr_alias(const dcg_node* input, char* out, size_t cap) noexcept nogil
    int c_dcg_node_expr_op_style(dcg_node* const* inputs, size_t n_inputs, dcg_op_code op, char* out, size_t cap) noexcept nogil
    int c_dcg_node_expr_func_style(dcg_node* const* inputs, size_t n_inputs, dcg_op_code op, const char* name, char* out, size_t cap) noexcept nogil


cdef class ExpressionNode(LogicNode):
    cdef PyObject** components

    cdef void c_components_alloc(self, size_t n_args)

    cdef void c_components_assign(self, size_t index, LogicNode node)


cdef class UnaryExpression(ExpressionNode):
    pass


cdef class BinaryExpression(ExpressionNode):
    pass


cdef class TernaryExpression(ExpressionNode):
    pass


cdef class CallExpression(ExpressionNode):
    pass
