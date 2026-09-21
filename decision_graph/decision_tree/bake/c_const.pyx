from cpython.unicode cimport PyUnicode_AsUTF8, PyUnicode_AsUTF8AndSize, PyUnicode_FromString

from .c_allocator_protocol cimport DCG_DEFAULT_ALLOCATOR
from .c_expr cimport BinaryExpression, UnaryExpression, dcg_op_code
from .c_node cimport c_dcg_node_set_repr
from .c_node import register_types
from .c_var cimport c_dcg_var_as_bool, c_dcg_var_as_double, c_dcg_var_as_int, c_dcg_var_as_string, c_dcg_var_is_null, c_dcg_var_pypack, dcg_ret_code, dcg_var_type, dcg_var_type_mask


cdef class ConstantNode(LogicNode):
    def __init__(self, object value, *, str repr=None):
        cdef dcg_constant_node* node = NULL
        cdef int ret_code

        if isinstance(value, bool):
            node = c_dcg_node_new_const_bool(value, DCG_DEFAULT_ALLOCATOR)
        elif isinstance(value, int):
            node = c_dcg_node_new_const_int(value, DCG_DEFAULT_ALLOCATOR)
        elif isinstance(value, float):
            node = c_dcg_node_new_const_double(value, DCG_DEFAULT_ALLOCATOR)
        elif isinstance(value, str):
            node = c_dcg_node_new_const_string(PyUnicode_AsUTF8(value), DCG_DEFAULT_ALLOCATOR)
        else:
            raise TypeError(f'A constant cannot hold {type(value).__name__}: bake has no node type for it.')

        if not node:
            raise MemoryError('Failed to allocate the ConstantNode.')

        self.header = &node.base
        self.owner = True

        if repr is not None:
            ret_code = c_dcg_node_set_repr(self.header, PyUnicode_AsUTF8(repr))
            if ret_code != dcg_ret_code.DCG_OK:
                raise RuntimeError(f'c_dcg_node_set_repr failed with err code: {ret_code}')

        self.c_register_node()

    # === Python Operators ===
    #
    # A constant composes into an expression the same way an expression does,
    # and it names the expression classes directly: the two families are the
    # concrete structures of one layer, and a sub-dependency inside a layer is
    # allowed (DEPENDENCY.md rule 2). The operand is the node base, which is the
    # one type both families can name.

    def __add__(self, LogicNode other):
        return BinaryExpression(dcg_op_code.DCG_OP_ADD, self, other)

    def __sub__(self, LogicNode other):
        return BinaryExpression(dcg_op_code.DCG_OP_SUB, self, other)

    def __mul__(self, LogicNode other):
        return BinaryExpression(dcg_op_code.DCG_OP_MUL, self, other)

    def __truediv__(self, LogicNode other):
        return BinaryExpression(dcg_op_code.DCG_OP_DIV, self, other)

    def __floordiv__(self, LogicNode other):
        return BinaryExpression(dcg_op_code.DCG_OP_FLOORDIV, self, other)

    def __pow__(self, LogicNode other):
        return BinaryExpression(dcg_op_code.DCG_OP_POW, self, other)

    def __neg__(self):
        return UnaryExpression(dcg_op_code.DCG_OP_NEG, self)

    def __lt__(self, LogicNode other):
        return BinaryExpression(dcg_op_code.DCG_OP_LT, self, other)

    def __le__(self, LogicNode other):
        return BinaryExpression(dcg_op_code.DCG_OP_LE, self, other)

    def __gt__(self, LogicNode other):
        return BinaryExpression(dcg_op_code.DCG_OP_GT, self, other)

    def __ge__(self, LogicNode other):
        return BinaryExpression(dcg_op_code.DCG_OP_GE, self, other)

    def __and__(self, LogicNode other):
        return BinaryExpression(dcg_op_code.DCG_OP_AND, self, other)

    def __or__(self, LogicNode other):
        return BinaryExpression(dcg_op_code.DCG_OP_OR, self, other)

    def __invert__(self):
        return UnaryExpression(dcg_op_code.DCG_OP_NOT, self)

    # === Python Properties ===

    property value:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')

            cdef const dcg_var_t* var = c_dcg_node_const_get(<dcg_constant_node*> self.header)
            cdef dcg_node_type ntype = self.header.ntype

            if ntype == dcg_node_type.DCG_NODE_TRUE:
                return True
            if ntype == dcg_node_type.DCG_NODE_FALSE:
                return False
            if ntype == dcg_node_type.DCG_NODE_INT:
                return c_dcg_var_as_int(var)
            if ntype == dcg_node_type.DCG_NODE_DOUBLE:
                return c_dcg_var_as_double(var)
            if ntype == dcg_node_type.DCG_NODE_STRING:
                return PyUnicode_FromString(c_dcg_var_as_string(var))
            return None

        def __set__(self, object value):
            import warnings
            warnings.warn('[DBG] Updating a const node value is debug only, do not use this in prod env. after the value update, a PermissionError will be raised intensionally!')

            cdef dcg_var_t slot
            c_dcg_var_pypack(&slot, value)

            cdef int ret_code = c_dcg_node_const_set(<dcg_constant_node*> self.header, slot)
            if ret_code != dcg_ret_code.DCG_OK:
                raise RuntimeError(f'c_dcg_node_const_set failed with err code: {ret_code}')

            warnings.warn(f'[DBG] {self} value slot updated! Raising PermissionError!')
            raise PermissionError(f'Must not update value of a {self.__class__.__name__}')


cdef class VariableNode(LogicNode):
    def __init__(self, *, str key=None, str repr=None):
        cdef dcg_variable_node* node
        cdef Py_ssize_t key_len = 0
        cdef const char* c_key = PyUnicode_AsUTF8AndSize(key, &key_len) if key else NULL
        cdef dcg_logic_group* active_logic_group = c_dcg_lgm_active_group(LogicNode.c_get_manager())

        node = c_dcg_node_new_var(
            PyUnicode_AsUTF8(repr) if repr is not None else NULL,
            c_key,
            key_len,
            NULL,
            active_logic_group,
            DCG_DEFAULT_ALLOCATOR,
        )
        if not node:
            raise MemoryError('Failed to allocate the VariableNode.')

        self.header = &node.base
        self.owner = True
        self.c_register_node()

    # === Cython Internal Binding ===

    cdef void c_bind_slot(self, dcg_var_t* slot):
        cdef int ret_code = c_dcg_node_var_bind(<dcg_variable_node*> self.header, slot)
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_node_var_bind failed with err code: {ret_code}')

    # === Python Interfaces ===

    cpdef void c_bind_const(self, ConstantNode node):
        self.c_bind_slot(&node.header.out)

    # === Python Properties ===

    property value:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')

            cdef dcg_var_t* slot = &self.header.out
            if c_dcg_var_is_null(slot):
                return None

            cdef dcg_var_type base = <dcg_var_type> ((<int> slot.dtype) & <int> dcg_var_type_mask.VAR_TYPE_BASE_MASK)

            if base == dcg_var_type.VAR_TYPE_BOOL:
                return c_dcg_var_as_bool(slot)
            if base == dcg_var_type.VAR_TYPE_INT:
                return c_dcg_var_as_int(slot)
            if base == dcg_var_type.VAR_TYPE_DOUBLE:
                return c_dcg_var_as_double(slot)
            if base == dcg_var_type.VAR_TYPE_STRING:
                return PyUnicode_FromString(c_dcg_var_as_string(slot))
            return None

    property key:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            cdef dcg_variable_node* node = <dcg_variable_node*> self.header
            if not node.key:
                return None
            return PyUnicode_FromString(node.key)


register_types({
    dcg_node_type.DCG_NODE_TRUE: ConstantNode,
    dcg_node_type.DCG_NODE_FALSE: ConstantNode,
    dcg_node_type.DCG_NODE_DOUBLE: ConstantNode,
    dcg_node_type.DCG_NODE_STRING: ConstantNode,
    dcg_node_type.DCG_NODE_INT: ConstantNode,
    dcg_node_type.DCG_NODE_VARIABLE: VariableNode,
})
