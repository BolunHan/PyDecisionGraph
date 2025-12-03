import json
import operator
import traceback

from cpython.mem cimport PyMem_Free

from .c_abc cimport LogicNodeFrame, LogicGroupStack, PlaceholderNode, ActionNode, LGM, NO_CONDITION, AUTO_CONDITION, NodeEdgeCondition
from .c_collection cimport LogicMapping, LogicSequence
from ..exc import NO_DEFAULT, TooManyChildren, TooFewChildren, EdgeValueError, ContextsNotFound, ExpressEvaluationError


cdef class NodeEvalPath(list):
    def to_clipboard(self):
        from pyperclip import copy as clipboard_copy
        cdef LogicNode node
        cdef list path = []
        for node in self:
            path.append(str(node.uid))
        cdef str payload = json.dumps(path)
        clipboard_copy(payload)
        return payload


cdef class RootLogicNode(LogicNode):
    def __cinit__(self, *, str name='Entry Point', object expression=None, type dtype=None, str repr=None, object uid=None, bint inherit_contexts=False, **kwargs):
        self.expression = True if expression is None else expression
        self.dtype = bool if dtype is None else dtype
        self.repr = name if repr is None else repr
        self.inherit_contexts = inherit_contexts
        self._eval_path = []

    cdef bint c_entry_check(self):
        return True

    cdef void c_on_enter(self):
        cdef LogicGroupStack* active_groups
        # Step 0: Append placeholder
        self.c_append(PlaceholderNode(auto_connect=False), NO_CONDITION)

        # Step 1: Pre-shelving enter
        LGM.c_ln_enter(self)

        # Step 2: Shelve LGM
        if self.inherit_contexts:
            active_groups = LGM._active_groups
            LGM.c_shelve()
            PyMem_Free(LGM._active_groups)
            LGM._active_groups = active_groups
        else:
            LGM.c_shelve()

        # Step 3: Mark as inspection_mode
        LGM.inspection_mode = True

        # Step 4: Post-shelving enter
        LGM.c_ln_enter(self)

    cdef void c_on_exit(self):
        cdef LogicGroupStack* active_groups
        self.c_consolidate_placeholder()
        LGM.c_ln_exit(self)
        # Prevent accidentally free the active_group when inherited
        if self.inherit_contexts:
            LGM._active_groups = NULL
        LGM.c_unshelve()
        LGM.c_ln_exit(self)

    cdef void c_append(self, LogicNode child, NodeEdgeCondition condition):
        if self.subordinates.size:
            raise TooManyChildren()

        if not (condition is AUTO_CONDITION or condition is NO_CONDITION):
            raise EdgeValueError()

        LogicNode.c_append(self, child, NO_CONDITION)

    def __call__(self, object default=None):
        self._eval_path.clear()
        cdef object value = self.c_eval_recursively(self._eval_path, default)[0]
        return value

    def eval_recursively(self, list path=None, object default=NO_DEFAULT):
        self._eval_path.clear()

        cdef object v
        cdef list p
        if path is None:
            v, p = self.c_eval_recursively(self._eval_path, default)
        else:
            v, p = self.c_eval_recursively(path, default)
            self._eval_path.extend(p)
        return v, p

    def dry_run(self, bint enforce_dtype=False):
        cdef LogicNode child
        for child in self.descendants:
            if isinstance(child, ActionNode):
                continue
            try:
                child.c_eval(enforce_dtype)
            except Exception as e:
                raise ExpressEvaluationError(f"Failed to evaluate {self}, {traceback.format_exc()}") from e

    cpdef BreakpointNode get_breakpoint(self):
        for leaf in self.leaves:
            if isinstance(leaf, BreakpointNode):
                return leaf
        return None

    def to_html(self, str file_name=None, bint with_eval=True):
        from ..webui import to_html
        to_html(self, file_name or f'{self.repr}.html', with_eval)

    def show(self, **kwargs):
        from ..webui import show
        show(self, **kwargs)

    def watch(self, **kwargs):
        from ..webui import watch
        watch(self, **kwargs)

    property child:
        def __get__(self) -> LogicNode:
            cdef LogicNodeFrame* frame = self.subordinates.top
            if frame:
                return <LogicNode> <object> frame.logic_node
            raise TooFewChildren()

    property eval_path:
        def __get__(self) -> NodeEvalPath:
            return NodeEvalPath(self._eval_path)


cdef class ContextLogicExpression(LogicNode):
    def __cinit__(self, *, LogicGroup logic_group=None, **kwargs):
        if logic_group is None:
            logic_group = LGM.active_group
            if logic_group is None:
                raise ContextsNotFound(f'Must assign a logic group or initialize {self.__class__.__name__} with in a LogicGroup with statement!')

        self.logic_group = logic_group

    @staticmethod
    cdef inline object c_safe_eval(object v):
        if isinstance(v, LogicNode):
            return (<LogicNode> v).c_eval(False)
        return v

    @staticmethod
    cdef inline str c_safe_alias(object v):
        if isinstance(v, LogicNode):
            return v.repr
        return str(v)

    # === Python Interfaces ===

    def __getitem__(self, str key):
        return AttrExpression(attr=key, logic_group=self.logic_group)

    def __getattr__(self, str key) -> AttrExpression:
        return AttrExpression(attr=key, logic_group=self.logic_group)

    # math operation to invoke MathExpression

    def __add__(self, object other):
        return MathExpression(left=self, op=MathExpressionOperator.add, right=other, logic_group=self.logic_group)

    def __sub__(self, object other):
        return MathExpression(left=self, op=MathExpressionOperator.sub, right=other, logic_group=self.logic_group)

    def __mul__(self, object other):
        return MathExpression(left=self, op=MathExpressionOperator.mul, right=other, logic_group=self.logic_group)

    def __truediv__(self, object other):
        return MathExpression(left=self, op=MathExpressionOperator.truediv, right=other, logic_group=self.logic_group)

    def __floordiv__(self, object other):
        return MathExpression(left=self, op=MathExpressionOperator.floordiv, right=other, logic_group=self.logic_group)

    def __pow__(self, object other):
        return MathExpression(left=self, op=MathExpressionOperator.pow, right=other, logic_group=self.logic_group)

    def __neg__(self):
        return MathExpression(left=self, op=MathExpressionOperator.neg, repr=f'-{self.repr}', logic_group=self.logic_group)

    # Comparison operation to invoke ComparisonExpression

    def __eq__(self, object other):
        return ComparisonExpression(left=self, op=ComparisonExpressionOperator.eq, right=other, logic_group=self.logic_group)

    def __ne__(self, object other):
        return ComparisonExpression(left=self, op=ComparisonExpressionOperator.ne, right=other, logic_group=self.logic_group)

    def __gt__(self, object other):
        return ComparisonExpression(left=self, op=ComparisonExpressionOperator.gt, right=other, logic_group=self.logic_group)

    def __ge__(self, object other):
        return ComparisonExpression(left=self, op=ComparisonExpressionOperator.ge, right=other, logic_group=self.logic_group)

    def __lt__(self, object other):
        return ComparisonExpression(left=self, op=ComparisonExpressionOperator.lt, right=other, logic_group=self.logic_group)

    def __le__(self, object other):
        return ComparisonExpression(left=self, op=ComparisonExpressionOperator.le, right=other, logic_group=self.logic_group)

    # Logical operation to invoke LogicalExpression

    def __and__(self, object other):
        return LogicalExpression(left=self, op=LogicalExpressionOperator.and_, right=other, logic_group=self.logic_group)

    def __or__(self, object other):
        return LogicalExpression(left=self, op=LogicalExpressionOperator.or_, right=other, logic_group=self.logic_group)

    def __invert__(self):
        return LogicalExpression(left=self, op=LogicalExpressionOperator.not_, repr=f'~{self.repr}', logic_group=self.logic_group)


cdef class AttrExpression(ContextLogicExpression):
    def __cinit__(self, *, str attr, **kwargs):
        self.attr = attr
        self.repr = kwargs['repr'] if 'repr' in kwargs else f'{self.logic_group.name}.{attr}'

    cdef object c_eval(self, bint enforce_dtype):
        if isinstance(self.logic_group, LogicMapping):
            return (<LogicMapping> self.logic_group).c_get(self.attr)
        else:
            if self.attr in self.logic_group.contexts:
                return self.logic_group.contexts[self.attr]
            raise AttributeError(f'Attribute {self.attr} does not exist in {self.logic_group}')

    def __getitem__(self, str key):
        return AttrNestedExpression(attrs=[self.attr, key], logic_group=self.logic_group)

    def __getattr__(self, str key) -> AttrNestedExpression:
        return AttrNestedExpression(attrs=[self.attr, key], logic_group=self.logic_group)


cdef class AttrNestedExpression(ContextLogicExpression):
    def __cinit__(self, *, list attrs, **kwargs):
        self.attrs = attrs
        self.repr = kwargs['repr'] if 'repr' in kwargs else f'{self.logic_group.name}.{".".join(attrs)}'

    cdef object c_eval(self, bint enforce_dtype):
        cdef object mapping
        if isinstance(self.logic_group, LogicMapping):
            mapping = (<LogicMapping> self.logic_group).data
        else:
            mapping = self.logic_group.contexts

        cdef str attr
        for attr in self.attrs:
            mapping = mapping[attr]
        return mapping

    def __getitem__(self, str key):
        return AttrNestedExpression(attrs=self.attrs + [key], logic_group=self.logic_group)

    def __getattr__(self, str key) -> AttrExpression:
        return AttrNestedExpression(attrs=self.attrs + [key], logic_group=self.logic_group)


cdef class GetterExpression(ContextLogicExpression):
    def __cinit__(self, *, object key, **kwargs):
        self.key = key
        self.repr = kwargs['repr'] if 'repr' in kwargs else f'{self.logic_group.name}.{key}'

    cdef object c_eval(self, bint enforce_dtype):
        if isinstance(self.logic_group, LogicMapping):
            return (<LogicMapping> self.logic_group).c_get(self.key)
        elif isinstance(self.logic_group, LogicSequence):
            return (<LogicSequence> self.logic_group).c_get(self.key)
        else:
            if self.key in self.logic_group.contexts:
                return self.logic_group.contexts[self.key]
            raise AttributeError(f'Attribute {self.key} does not exist in {self.logic_group}')

    def __getitem__(self, str key):
        return GetterNestedExpression(keys=[self.key, key], logic_group=self.logic_group)


cdef class GetterNestedExpression(ContextLogicExpression):
    def __cinit__(self, *, list keys, **kwargs):
        self.keys = keys
        self.repr = kwargs['repr'] if 'repr' in kwargs else f'{self.logic_group.name}.{".".join(keys)}'

    cdef object c_eval(self, bint enforce_dtype):
        cdef object nested
        if isinstance(self.logic_group, LogicMapping):
            nested = (<LogicMapping> self.logic_group).data
        elif isinstance(self.logic_group, LogicSequence):
            return (<LogicSequence> self.logic_group).c_get(self.key)
        else:
            nested = self.logic_group.contexts

        cdef object key
        for key in self.keys:
            nested = nested[key]
        return nested

    def __getitem__(self, str key):
        return GetterNestedExpression(keys=self.keys + [key], logic_group=self.logic_group)


cdef class MathExpressionOperator:
    add = MathExpressionOperator.__new__(MathExpressionOperator, 'add', '+')
    sub = MathExpressionOperator.__new__(MathExpressionOperator, 'sub', '-')
    mul = MathExpressionOperator.__new__(MathExpressionOperator, 'mul', '*')
    truediv = MathExpressionOperator.__new__(MathExpressionOperator, 'truediv', '/')
    floordiv = MathExpressionOperator.__new__(MathExpressionOperator, 'floordiv', '//')
    pow = MathExpressionOperator.__new__(MathExpressionOperator, 'pow', '**')
    neg = MathExpressionOperator.__new__(MathExpressionOperator, 'neg', '--')

    def __cinit__(self, str name, str op):
        self.name = name
        self.value = op

    def to_func(self):
        return getattr(operator, self.name)

    @classmethod
    def from_str(cls, str op_str):
        cdef MathExpressionOperator op
        for op in [cls.add, cls.sub, cls.mul, cls.truediv, cls.floordiv, cls.pow, cls.neg]:
            if op.name == op_str:
                return op
            elif op.value == op_str:
                return op
        raise ValueError(f'Unknown MathExpressionOperator: {op_str}')


cdef class MathExpression(ContextLogicExpression):
    def __cinit__(self, *, object left, object op, object right=NO_DEFAULT, **kwargs):
        self.left = left
        self.right = right
        self.dtype = kwargs.get('dtype', float)
        cdef MathExpressionOperator _op

        if isinstance(op, MathExpressionOperator):
            _op = <MathExpressionOperator> op
            self.op_name = kwargs.get('op_name', _op.name)
            self.op_repr = kwargs.get('op_repr', _op.value)
            self.op_func = _op.to_func()
            self.repr = kwargs.get('repr', self.c_op_style_repr())
        elif isinstance(op, str):
            _op = MathExpressionOperator.from_str(op)
            self.op_name = kwargs.get('op_name', _op.name)
            self.op_repr = kwargs.get('op_name', _op.value)
            self.op_func = _op.to_func()
            self.repr = kwargs.get('repr', self.c_op_style_repr())
        elif callable(op):
            self.op_name = op.__name__
            self.op_repr = op.__name__
            self.op_func = op
            self.repr = kwargs.get('repr', self.c_func_style_repr())
        else:
            raise TypeError(f'Expected op to be MathExpressionOperator, str or callable, got {type(op).__name__} instead.')

    cdef str c_op_style_repr(self):
        if self.right is NO_DEFAULT:
            return f'{self.op_repr}{ContextLogicExpression.c_safe_alias(self.left)}'
        return f'{ContextLogicExpression.c_safe_alias(self.left)} {self.op_repr} {ContextLogicExpression.c_safe_alias(self.right)}'

    cdef str c_func_style_repr(self):
        if self.right is NO_DEFAULT:
            return f'{self.op_repr}({ContextLogicExpression.c_safe_alias(self.left)})'
        return f'{self.op_repr}({ContextLogicExpression.c_safe_alias(self.left)}, {ContextLogicExpression.c_safe_alias(self.right)})'

    cdef object c_eval(self, bint enforce_dtype):
        if self.right is NO_DEFAULT:
            return self.op_func(ContextLogicExpression.c_safe_eval(self.left))
        return self.op_func(
            ContextLogicExpression.c_safe_eval(self.left),
            ContextLogicExpression.c_safe_eval(self.right)
        )


cdef class ComparisonExpressionOperator:
    eq = ComparisonExpressionOperator.__new__(ComparisonExpressionOperator, 'eq', '==', 1)
    ne = ComparisonExpressionOperator.__new__(ComparisonExpressionOperator, 'ne', '!=', 2)
    gt = ComparisonExpressionOperator.__new__(ComparisonExpressionOperator, 'gt', '>', 3)
    ge = ComparisonExpressionOperator.__new__(ComparisonExpressionOperator, 'ge', '>=', 4)
    lt = ComparisonExpressionOperator.__new__(ComparisonExpressionOperator, 'lt', '<', 5)
    le = ComparisonExpressionOperator.__new__(ComparisonExpressionOperator, 'le', '<=', 6)

    def __cinit__(self, str name, str op, uint8_t int_enum):
        self.name = name
        self.value = op
        self.int_enum = int_enum

    def to_func(self):
        return getattr(operator, self.name)

    @classmethod
    def from_str(cls, str op_str):
        cdef ComparisonExpressionOperator op
        for op in [cls.eq, cls.ne, cls.gt, cls.ge, cls.lt, cls.le]:
            if op.name == op_str:
                return op
            elif op.value == op_str:
                return op
        raise ValueError(f'Unknown ComparisonExpressionOperator: {op_str}')


cdef class ComparisonExpression(ContextLogicExpression):
    def __cinit__(self, *, object left, object op, object right, **kwargs):
        self.left = left
        self.right = right
        self.dtype = kwargs.get('dtype', bool)
        cdef ComparisonExpressionOperator _op

        if isinstance(op, ComparisonExpressionOperator):
            _op = <ComparisonExpressionOperator> op
            self.op_name = kwargs.get('op_name', _op.name)
            self.op_repr = kwargs.get('op_repr', _op.value)
            self.op_func = _op.to_func()
            self.repr = kwargs.get('repr', self.c_op_style_repr())
            self.op_enum = _op.int_enum
        elif isinstance(op, str):
            _op = ComparisonExpressionOperator.from_str(op)
            self.op_name = kwargs.get('op_name', _op.name)
            self.op_repr = kwargs.get('op_name', _op.value)
            self.op_func = _op.to_func()
            self.repr = kwargs.get('repr', self.c_op_style_repr())
            self.op_enum = _op.int_enum
        elif callable(op):
            self.op_name = op.__name__
            self.op_repr = op.__name__
            self.op_func = op
            self.repr = kwargs.get('repr', self.c_func_style_repr())
            self.builtin = False
            self.op_enum = 0
        else:
            raise TypeError(f'Expected op to be ComparisonExpressionOperator, str or callable, got {type(op).__name__} instead.')

    cdef str c_op_style_repr(self):
        if self.right is NO_DEFAULT:
            return f'{self.op_repr}{ContextLogicExpression.c_safe_alias(self.left)}'
        return f'{ContextLogicExpression.c_safe_alias(self.left)} {self.op_repr} {ContextLogicExpression.c_safe_alias(self.right)}'

    cdef str c_func_style_repr(self):
        if self.right is NO_DEFAULT:
            return f'{self.op_repr}({ContextLogicExpression.c_safe_alias(self.left)})'
        return f'{self.op_repr}({ContextLogicExpression.c_safe_alias(self.left)}, {ContextLogicExpression.c_safe_alias(self.right)})'

    cdef object c_eval(self, bint enforce_dtype):
        cdef object left = ContextLogicExpression.c_safe_eval(self.left)
        cdef object right = ContextLogicExpression.c_safe_eval(self.right)
        cdef uint8_t op_enum = self.op_enum

        if op_enum == 0:
            return self.op_func(left, right)
        elif op_enum == 1:
            return left == right
        elif op_enum == 2:
            return left != right
        elif op_enum == 3:
            return left > right
        elif op_enum == 4:
            return left >= right
        elif op_enum == 5:
            return left < right
        elif op_enum == 6:
            return left <= right
        else:
            raise RuntimeError(f'Invalid comparison op_enum {op_enum}')


cdef class LogicalExpressionOperator:
    and_ = LogicalExpressionOperator.__new__(LogicalExpressionOperator, 'and_', '&', 1)
    or_ = LogicalExpressionOperator.__new__(LogicalExpressionOperator, 'or_', '|', 2)
    not_ = LogicalExpressionOperator.__new__(LogicalExpressionOperator, 'not_', '~', 3)

    def __cinit__(self, str name, str op, uint8_t int_enum):
        self.name = name
        self.value = op
        self.int_enum = int_enum

    def to_func(self):
        return getattr(operator, self.name)

    @classmethod
    def from_str(cls, str op_str):
        cdef LogicalExpressionOperator op
        for op in [cls.and_, cls.or_, cls.not_]:
            if op.name == op_str:
                return op
            elif op.value == op_str:
                return op
        raise ValueError(f'Unknown LogicalExpressionOperator: {op_str}')


cdef class LogicalExpression(ContextLogicExpression):
    def __cinit__(self, *, object left, object op, object right=NO_DEFAULT, **kwargs):
        self.left = left
        self.right = right
        self.dtype = kwargs.get('dtype', bool)
        cdef LogicalExpressionOperator _op

        if isinstance(op, LogicalExpressionOperator):
            _op = <LogicalExpressionOperator> op
            self.op_name = kwargs.get('op_name', _op.name)
            self.op_repr = kwargs.get('op_repr', _op.value)
            self.op_func = _op.to_func()
            self.repr = kwargs.get('repr', self.c_op_style_repr())
            self.op_enum = _op.int_enum
        elif isinstance(op, str):
            _op = LogicalExpressionOperator.from_str(op)
            self.op_name = kwargs.get('op_name', _op.name)
            self.op_repr = kwargs.get('op_name', _op.value)
            self.op_func = _op.to_func()
            self.repr = kwargs.get('repr', self.c_op_style_repr())
            self.op_enum = _op.int_enum
        elif callable(op):
            self.op_name = op.__name__
            self.op_repr = op.__name__
            self.op_func = op
            self.repr = kwargs.get('repr', self.c_func_style_repr())
            self.builtin = False
            self.op_enum = 0
        else:
            raise TypeError(f'Expected op to be LogicalExpressionOperator, str or callable, got {type(op).__name__} instead.')

    cdef str c_op_style_repr(self):
        if self.right is NO_DEFAULT:
            return f'{self.op_repr}{ContextLogicExpression.c_safe_alias(self.left)}'
        return f'{ContextLogicExpression.c_safe_alias(self.left)} {self.op_repr} {ContextLogicExpression.c_safe_alias(self.right)}'

    cdef str c_func_style_repr(self):
        if self.right is NO_DEFAULT:
            return f'{self.op_repr}({ContextLogicExpression.c_safe_alias(self.left)})'
        return f'{self.op_repr}({ContextLogicExpression.c_safe_alias(self.left)}, {ContextLogicExpression.c_safe_alias(self.right)})'

    cdef object c_eval(self, bint enforce_dtype):
        cdef uint8_t op_enum = self.op_enum

        if op_enum == 0:
            return self.op_func(ContextLogicExpression.c_safe_eval(self.left), ContextLogicExpression.c_safe_eval(self.right))

        cdef bint left = ContextLogicExpression.c_safe_eval(self.left)

        if op_enum == 3:
            return not left

        cdef bint right = ContextLogicExpression.c_safe_eval(self.right)

        if op_enum == 1:
            return left and right
        elif op_enum == 2:
            return left or right

        raise RuntimeError(f'Invalid comparison op_enum {op_enum}')
