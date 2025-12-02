from cpython.object cimport PyObject
from cpython.pystate cimport PyThreadState

cdef extern from "Python.h":
    void PyThreadState_EnterTracing(PyThreadState* tstate)
    void PyThreadState_LeaveTracing(PyThreadState* tstate)


cdef dict GLOBAL_SINGLETON


cdef class Singleton:
    pass


cdef class NodeEdgeCondition(Singleton):
    cdef PyObject* value_addr


cdef class ConditionElse(NodeEdgeCondition):
    pass


cdef class ConditionAny(NodeEdgeCondition):
    pass


cdef class ConditionAuto(NodeEdgeCondition):
    pass


cdef class BinaryCondition(NodeEdgeCondition):
    pass


cdef class ConditionTrue(BinaryCondition):
    pass


cdef class ConditionFalse(BinaryCondition):
    pass


cdef ConditionAny NO_CONDITION
cdef ConditionElse ELSE_CONDITION
cdef ConditionAuto AUTO_CONDITION
cdef ConditionTrue TRUE_CONDITION
cdef ConditionFalse FALSE_CONDITION


cdef class SkipContextsBlock:
    cdef public bint default_entry_check

    cdef type skip_exception
    cdef bint tracer_override
    cdef object cframe
    cdef tuple enter_line

    cdef size_t cframe_tracer_sig_count
    cdef object cframe_tracer

    cdef size_t global_tracer_sig_count
    cdef object global_tracer

    cdef size_t global_profiler_sig_count
    cdef object global_profiler

    cdef bint c_entry_check(self)

    cdef void c_on_enter(self)

    cdef void c_on_exit(self)


cdef class LogicExpression(SkipContextsBlock):
    cdef readonly object expression
    cdef readonly type dtype
    cdef readonly str repr
    cdef readonly object uid

    cdef object c_eval(self, bint enforce_dtype)

    @staticmethod
    cdef LogicExpression c_cast(object value, type dtype)

    @staticmethod
    cdef LogicExpression c_math_op(LogicExpression self, object other, object op, str operator_str, type dtype)


cdef struct LogicGroupFrame:
    PyObject* logic_group
    LogicGroupFrame* prev


cdef struct LogicGroupStack:
    LogicGroupFrame* top
    size_t size


cdef struct LogicNodeFrame:
    PyObject* logic_node
    LogicNodeFrame* prev


cdef struct LogicNodeStack:
    LogicNodeFrame* top
    size_t size


cdef struct ShelvedStateFrame:
    LogicGroupStack* active_groups
    LogicNodeStack* active_nodes
    LogicNodeStack* breakpoint_nodes
    LogicNodeStack* active_breakpoints
    bint inspection_mode
    bint vigilant_mode
    ShelvedStateFrame* prev


cdef struct ShelvedStateStack:
    ShelvedStateFrame* top
    size_t size


cdef class LogicGroupManager(Singleton):
    cdef LogicGroupStack* _active_groups
    cdef LogicNodeStack* _active_nodes
    cdef LogicNodeStack* _breakpoint_nodes
    cdef ShelvedStateStack* _shelved_state

    cdef readonly dict _cache
    cdef public bint inspection_mode
    cdef public bint vigilant_mode

    @staticmethod
    cdef inline void c_ln_stack_push(LogicNodeStack* stack, LogicNode logic_node)

    @staticmethod
    cdef inline void c_ln_stack_pop(LogicNodeStack* stack, LogicNode logic_node=*)

    @staticmethod
    cdef inline void c_ln_stack_remove(LogicNodeStack* stack, LogicNode logic_node)

    @staticmethod
    cdef inline LogicNodeFrame* c_ln_stack_locate(LogicNodeStack* stack, LogicNode logic_node)

    @staticmethod
    cdef inline void c_ln_stack_relocate(LogicNodeStack* original_stack, LogicNodeStack* new_stack, LogicNode logic_node)

    cdef inline LogicGroup c_cached_init(self, str name, type cls, dict kwargs)

    cdef inline void c_lg_enter(self, LogicGroup logic_group)

    cdef inline void c_lg_exit(self, LogicGroup logic_group=*)

    cdef inline void c_ln_enter(self, LogicNode logic_node)

    cdef inline void c_ln_exit(self, LogicNode logic_node)

    cdef inline void c_shelve(self)

    cdef inline void c_unshelve(self)

    cdef inline void c_clear(self)


cdef LogicGroupManager LGM


cdef class LogicGroup:
    cdef readonly str name
    cdef readonly LogicGroup parent
    cdef readonly type Break
    cdef readonly dict contexts

    cdef void c_break_inspection(self)

    cdef void c_break_active(self)

    cdef void c_break_runtime(self)


cdef class LogicNode(LogicExpression):
    cdef LogicNodeStack* subordinates
    cdef readonly NodeEdgeCondition condition_to_parent
    cdef readonly LogicNode parent
    cdef readonly dict children
    cdef readonly list labels
    cdef readonly bint autogen

    cdef NodeEdgeCondition c_infer_condition(self, LogicNode child)

    cdef PlaceholderNode c_get_placeholder(self)

    cdef void c_append(self, LogicNode child, NodeEdgeCondition condition)

    cdef void c_overwrite(self, LogicNode new_node, NodeEdgeCondition condition)

    cdef void c_replace(self, LogicNode original_node, LogicNode new_node)

    cdef void c_validate(self)

    cdef tuple c_eval_recursively(self, list path=*, object default=*)

    cdef void c_auto_fill(self)

    cdef size_t c_consolidate_placeholder(self)


cdef class BreakpointNode(LogicNode):
    cdef readonly LogicGroup break_from
    cdef readonly bint await_connection

    cdef void c_connect(self, LogicNode child)


cdef class ActionNode(LogicNode):
    cdef readonly object action

    cdef void c_auto_connect(self)

    cdef void c_post_eval(self)


cdef class PlaceholderNode(ActionNode):
    pass


cdef class NoAction(ActionNode):
    cdef readonly ssize_t sig


cdef class LongAction(ActionNode):
    cdef readonly ssize_t sig


cdef class ShortAction(ActionNode):
    cdef readonly ssize_t sig


cdef class CancelAction(ActionNode):
    cdef readonly ssize_t sig


cdef class ClearAction(ActionNode):
    cdef readonly ssize_t sig
