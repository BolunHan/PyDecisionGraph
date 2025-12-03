from libc.stdint cimport uint8_t

from .c_abc cimport LogicNode, LogicGroup, BreakpointNode


cdef class NodeEvalPath(list):
    pass


cdef class RootLogicNode(LogicNode):
    cdef readonly bint inherit_contexts
    cdef readonly list _eval_path

    cpdef BreakpointNode get_breakpoint(self)


cdef class ContextLogicExpression(LogicNode):
    cdef readonly LogicGroup logic_group

    @staticmethod
    cdef inline object c_safe_eval(object v)

    @staticmethod
    cdef inline str c_safe_alias(object v)


cdef class AttrExpression(ContextLogicExpression):
    cdef readonly str attr


cdef class AttrNestedExpression(ContextLogicExpression):
    cdef readonly list attrs


cdef class GetterExpression(ContextLogicExpression):
    cdef readonly object key


cdef class GetterNestedExpression(ContextLogicExpression):
    cdef readonly list keys


cdef class MathExpressionOperator:
    cdef readonly str name
    cdef readonly str value
    cdef readonly str op


cdef class MathExpression(ContextLogicExpression):
    cdef readonly str op_name
    cdef readonly str op_repr
    cdef readonly object op_func
    cdef readonly object left
    cdef readonly object right

    cdef str c_op_style_repr(self)
    cdef str c_func_style_repr(self)


cdef class ComparisonExpressionOperator:
    cdef readonly str name
    cdef readonly str value
    cdef readonly str op
    cdef uint8_t int_enum


cdef class ComparisonExpression(ContextLogicExpression):
    cdef readonly str op_name
    cdef readonly str op_repr
    cdef readonly object op_func
    cdef readonly object left
    cdef readonly object right
    cdef uint8_t op_enum

    cdef str c_op_style_repr(self)
    cdef str c_func_style_repr(self)


cdef class LogicalExpressionOperator:
    cdef readonly str name
    cdef readonly str value
    cdef readonly str op
    cdef uint8_t int_enum


cdef class LogicalExpression(ContextLogicExpression):
    cdef readonly str op_name
    cdef readonly str op_repr
    cdef readonly object op_func
    cdef readonly object left
    cdef readonly object right
    cdef uint8_t op_enum

    cdef str c_op_style_repr(self)
    cdef str c_func_style_repr(self)
