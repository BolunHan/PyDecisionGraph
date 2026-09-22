import enum

from cpython.mem cimport PyMem_Calloc, PyMem_Free, PyMem_Malloc
from cpython.ref cimport Py_XDECREF, Py_XINCREF
from cpython.unicode cimport PyUnicode_AsUTF8

from .c_allocator_protocol cimport DCG_DEFAULT_ALLOCATOR
from .c_var cimport dcg_ret_code


class ExpressionOperator(enum.IntEnum):
    none = DCG_OP_NONE

    arith = DCG_OP_ARITH
    add = DCG_OP_ADD
    sub = DCG_OP_SUB
    mul = DCG_OP_MUL
    div = DCG_OP_DIV
    floordiv = DCG_OP_FLOORDIV
    pow = DCG_OP_POW
    neg = DCG_OP_NEG

    compare = DCG_OP_COMPARE
    eq = DCG_OP_EQ
    ne = DCG_OP_NE
    gt = DCG_OP_GT
    ge = DCG_OP_GE
    lt = DCG_OP_LT
    le = DCG_OP_LE

    logic = DCG_OP_LOGIC
    and_ = DCG_OP_AND
    or_ = DCG_OP_OR
    not_ = DCG_OP_NOT

    access = DCG_OP_ACCESS
    attr = DCG_OP_ATTR
    getitem = DCG_OP_GETITEM


cdef class ExpressionNode(LogicNode):
    def __init__(self, size_t n_args=DCG_EXPR_DEFAULT_ARGS, dcg_node_type node_type=dcg_node_type.DCG_NODE_OP):
        cdef dcg_expression_node* node = c_dcg_node_new_expr(n_args, node_type, DCG_DEFAULT_ALLOCATOR)
        if not node:
            raise MemoryError(f'Failed to allocate the {self.__class__.__name__}.')

        self.header = &node.base
        self.owner = True
        self.c_components_alloc(node.n_args)
        self.c_register_node()

    def __dealloc__(self):
        cdef size_t i
        cdef size_t n_args = DCG_EXPR_DEFAULT_ARGS

        if self.header:
            n_args = (<dcg_expression_node*> self.header).n_args

        if self.components:
            for i in range(n_args):
                Py_XDECREF(self.components[i])

        PyMem_Free(self.components)

    # === Cython Internal Binding ===

    cdef void c_components_alloc(self, size_t n_args):
        self.components = <PyObject**> PyMem_Calloc(n_args, sizeof(PyObject*))
        if not self.components:
            raise MemoryError(f'Failed to allocate the operand array of the {self.__class__.__name__}.')

    cdef void c_components_assign(self, size_t index, LogicNode node):
        if self.components[index] == <PyObject*> node:
            return
        Py_XINCREF(<PyObject*> node)
        Py_XDECREF(self.components[index])
        self.components[index] = <PyObject*> node

    # === Python Interfaces ===

    def bind(self, size_t index, LogicNode node):
        cdef int ret_code = c_dcg_node_expr_bind(<dcg_expression_node*> self.header, index, node.header)
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_node_expr_bind failed with err code: {ret_code}')
        self.c_components_assign(index, node)

    # === Python Operators ===

    def __add__(self, LogicNode other):
        return BinaryExpression(DCG_OP_ADD, self, other)

    def __sub__(self, LogicNode other):
        return BinaryExpression(DCG_OP_SUB, self, other)

    def __mul__(self, LogicNode other):
        return BinaryExpression(DCG_OP_MUL, self, other)

    def __truediv__(self, LogicNode other):
        return BinaryExpression(DCG_OP_DIV, self, other)

    def __floordiv__(self, LogicNode other):
        return BinaryExpression(DCG_OP_FLOORDIV, self, other)

    def __pow__(self, LogicNode other):
        return BinaryExpression(DCG_OP_POW, self, other)

    def __neg__(self):
        return UnaryExpression(DCG_OP_NEG, self)

    def __eq__(self, LogicNode other):
        return BinaryExpression(DCG_OP_EQ, self, other)

    def __ne__(self, LogicNode other):
        return BinaryExpression(DCG_OP_NE, self, other)

    def __hash__(self):
        return hash(self.address)

    # Ordered comparisons build comparison nodes.
    def __lt__(self, LogicNode other):
        return BinaryExpression(DCG_OP_LT, self, other)

    def __le__(self, LogicNode other):
        return BinaryExpression(DCG_OP_LE, self, other)

    def __gt__(self, LogicNode other):
        return BinaryExpression(DCG_OP_GT, self, other)

    def __ge__(self, LogicNode other):
        return BinaryExpression(DCG_OP_GE, self, other)

    def __and__(self, LogicNode other):
        return BinaryExpression(DCG_OP_AND, self, other)

    def __or__(self, LogicNode other):
        return BinaryExpression(DCG_OP_OR, self, other)

    def __invert__(self):
        return UnaryExpression(DCG_OP_NOT, self)

    property op:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            cdef dcg_expression_node* node = <dcg_expression_node*> self.header
            return ExpressionOperator(node.op)

    property n_args:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            cdef dcg_expression_node* node = <dcg_expression_node*> self.header
            return node.n_args


cdef class UnaryExpression(ExpressionNode):
    def __init__(self, dcg_op_code op, LogicNode src):
        cdef dcg_expression_node* node = c_dcg_node_new_expr_unary(op, src.header, DCG_DEFAULT_ALLOCATOR)
        if not node:
            raise MemoryError('Failed to allocate the UnaryExpression.')

        self.header = &node.base
        self.owner = True
        self.c_components_alloc(1)
        self.c_components_assign(0, src)
        self.c_register_node()


cdef class BinaryExpression(ExpressionNode):
    def __init__(self, dcg_op_code op, LogicNode var_0, LogicNode var_1):
        cdef dcg_expression_node* node = c_dcg_node_new_expr_binary(op, var_0.header, var_1.header, DCG_DEFAULT_ALLOCATOR)
        if not node:
            raise MemoryError('Failed to allocate the BinaryExpression.')

        self.header = &node.base
        self.owner = True
        self.c_components_alloc(2)
        self.c_components_assign(0, var_0)
        self.c_components_assign(1, var_1)
        self.c_register_node()


cdef class TernaryExpression(ExpressionNode):
    def __init__(self, dcg_op_code op, LogicNode var_0, LogicNode var_1, LogicNode var_2):
        cdef dcg_expression_node* node = c_dcg_node_new_expr_ternary(op, var_0.header, var_1.header, var_2.header, DCG_DEFAULT_ALLOCATOR)
        if not node:
            raise MemoryError('Failed to allocate the TernaryExpression.')

        self.header = &node.base
        self.owner = True
        self.c_components_alloc(3)
        self.c_components_assign(0, var_0)
        self.c_components_assign(1, var_1)
        self.c_components_assign(2, var_2)
        self.c_register_node()


cdef class CallExpression(ExpressionNode):
    def __init__(self, dcg_op_code op, object inputs, str name=None):
        cdef size_t n_vars = len(inputs)
        cdef dcg_node** vars
        cdef dcg_expression_node* node = NULL
        cdef size_t i
        cdef object argument

        if n_vars == 0:
            raise ValueError('A CallExpression needs at least one input.')

        vars = <dcg_node**> PyMem_Malloc(n_vars * sizeof(dcg_node*))
        if not vars:
            raise MemoryError('Failed to allocate the argument array.')
        try:
            for i in range(n_vars):
                argument = inputs[i]
                if not isinstance(argument, LogicNode):
                    raise TypeError(f'CallExpression argument {i} is not a LogicNode, but {type(argument).__name__}.')
                vars[i] = (<LogicNode> argument).header
            node = c_dcg_node_new_expr_call(
                op, vars, n_vars,
                PyUnicode_AsUTF8(name) if name is not None else NULL,
                DCG_DEFAULT_ALLOCATOR,
            )
        finally:
            PyMem_Free(vars)

        if not node:
            raise MemoryError('Failed to allocate the CallExpression.')

        self.header = &node.base
        self.owner = True
        self.c_components_alloc(n_vars)
        for i in range(n_vars):
            self.c_components_assign(i, <LogicNode> inputs[i])
        self.c_register_node()
