from cpython.unicode cimport PyUnicode_AsUTF8, PyUnicode_AsUTF8AndSize, PyUnicode_FromString

from .c_allocator_protocol cimport DCG_DEFAULT_ALLOCATOR
from .c_expr cimport BinaryExpression, UnaryExpression, dcg_op_code
from .c_node cimport c_dcg_node_pypack, c_dcg_node_set_repr
from .c_var cimport c_dcg_var_pypack, c_dcg_var_pyunpack, dcg_ret_code


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

    def __add__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_ADD, self, c_dcg_node_pypack(other))

    def __radd__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_ADD, c_dcg_node_pypack(other), self)

    def __sub__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_SUB, self, c_dcg_node_pypack(other))

    def __rsub__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_SUB, c_dcg_node_pypack(other), self)

    def __mul__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_MUL, self, c_dcg_node_pypack(other))

    def __rmul__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_MUL, c_dcg_node_pypack(other), self)

    def __truediv__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_DIV, self, c_dcg_node_pypack(other))

    def __rtruediv__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_DIV, c_dcg_node_pypack(other), self)

    def __floordiv__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_FLOORDIV, self, c_dcg_node_pypack(other))

    def __rfloordiv__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_FLOORDIV, c_dcg_node_pypack(other), self)

    def __pow__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_POW, self, c_dcg_node_pypack(other))

    def __rpow__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_POW, c_dcg_node_pypack(other), self)

    def __neg__(self):
        return UnaryExpression(dcg_op_code.DCG_OP_NEG, self)

    def __eq__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_EQ, self, c_dcg_node_pypack(other))

    def __ne__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_NE, self, c_dcg_node_pypack(other))

    def __hash__(self):
        return hash(self.address)

    def __lt__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_LT, self, c_dcg_node_pypack(other))

    def __le__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_LE, self, c_dcg_node_pypack(other))

    def __gt__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_GT, self, c_dcg_node_pypack(other))

    def __ge__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_GE, self, c_dcg_node_pypack(other))

    def __and__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_AND, self, c_dcg_node_pypack(other))

    def __rand__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_AND, c_dcg_node_pypack(other), self)

    def __or__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_OR, self, c_dcg_node_pypack(other))

    def __ror__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_OR, c_dcg_node_pypack(other), self)

    def __invert__(self):
        return UnaryExpression(dcg_op_code.DCG_OP_NOT, self)

    # === Python Properties ===

    property value:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return c_dcg_var_pyunpack(c_dcg_node_const_get(<dcg_constant_node*> self.header))

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
        # With a group given the node is a block OF THAT GROUP, and the group
        # releases it - so the wrapper owns nothing and must free nothing.
        self.owner = active_logic_group == NULL
        self.c_register_node()

    # === Cython Internal Binding ===

    cdef void c_bind_slot(self, dcg_var_t* slot):
        cdef int ret_code = c_dcg_node_var_bind(<dcg_variable_node*> self.header, slot)
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_node_var_bind failed with err code: {ret_code}')

    # === Python Interfaces ===

    cpdef void c_bind_const(self, ConstantNode node):
        self.c_bind_slot(&node.header.out)

    # === Python Operators ===

    def __add__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_ADD, self, c_dcg_node_pypack(other))

    def __radd__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_ADD, c_dcg_node_pypack(other), self)

    def __sub__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_SUB, self, c_dcg_node_pypack(other))

    def __rsub__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_SUB, c_dcg_node_pypack(other), self)

    def __mul__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_MUL, self, c_dcg_node_pypack(other))

    def __rmul__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_MUL, c_dcg_node_pypack(other), self)

    def __truediv__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_DIV, self, c_dcg_node_pypack(other))

    def __rtruediv__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_DIV, c_dcg_node_pypack(other), self)

    def __floordiv__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_FLOORDIV, self, c_dcg_node_pypack(other))

    def __rfloordiv__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_FLOORDIV, c_dcg_node_pypack(other), self)

    def __pow__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_POW, self, c_dcg_node_pypack(other))

    def __rpow__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_POW, c_dcg_node_pypack(other), self)

    def __neg__(self):
        return UnaryExpression(dcg_op_code.DCG_OP_NEG, self)

    def __eq__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_EQ, self, c_dcg_node_pypack(other))

    def __ne__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_NE, self, c_dcg_node_pypack(other))

    def __hash__(self):
        return hash(self.address)

    def __lt__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_LT, self, c_dcg_node_pypack(other))

    def __le__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_LE, self, c_dcg_node_pypack(other))

    def __gt__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_GT, self, c_dcg_node_pypack(other))

    def __ge__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_GE, self, c_dcg_node_pypack(other))

    def __and__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_AND, self, c_dcg_node_pypack(other))

    def __rand__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_AND, c_dcg_node_pypack(other), self)

    def __or__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_OR, self, c_dcg_node_pypack(other))

    def __ror__(self, object other):
        return BinaryExpression(dcg_op_code.DCG_OP_OR, c_dcg_node_pypack(other), self)

    def __invert__(self):
        return UnaryExpression(dcg_op_code.DCG_OP_NOT, self)

    # === Python Properties ===

    property value:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return c_dcg_var_pyunpack(&self.header.out)

    property key:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            cdef dcg_variable_node* node = <dcg_variable_node*> self.header
            if not node.key:
                return None
            return PyUnicode_FromString(node.key)
