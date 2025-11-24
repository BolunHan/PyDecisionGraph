import operator

from .c_abc cimport LogicNodeFrame, PlaceholderNode, LGM, NO_CONDITION, AUTO_CONDITION, NodeEdgeCondition
from .c_collection cimport LogicMapping
from ..exc import NO_DEFAULT, TooManyChildren, TooFewChildren, EdgeValueError, ContextsNotFound


cdef class RootLogicNode(LogicNode):
    def __cinit__(self, *, **kwargs):
        self.expression = True
        self.dtype = bool
        self.repr = 'Entry Point'

    cdef bint c_entry_check(self):
        return True

    cdef void c_on_enter(self):
        self.c_append(PlaceholderNode(auto_connect=False), NO_CONDITION)

        # Pre-shelving entering
        LGM.c_ln_enter(self)

        LGM.c_shelve()
        LGM.inspection_mode = True

        # Post-shelving entering
        LGM.c_ln_enter(self)

    cdef void c_on_exit(self):
        self.c_consolidate_placeholder()
        LGM.c_ln_exit(self)
        LGM.c_unshelve()

    cdef void c_append(self, LogicNode child, NodeEdgeCondition condition):
        if self.subordinates.size:
            raise TooManyChildren()

        if not (condition is AUTO_CONDITION or condition is NO_CONDITION):
            raise EdgeValueError()

        LogicNode.c_append(self, child, NO_CONDITION)

    def eval_recursively(self, **kwargs):
        return self.child.eval_recursively(**kwargs)

    def to_html(self, with_group=True, dry_run=True, filename="decision_graph.html", **kwargs):
        return self.child.to_html(with_group=with_group, dry_run=dry_run, filename=filename, **kwargs)

    @property
    def child(self) -> LogicNode:
        cdef LogicNodeFrame* frame = self.subordinates.top
        if frame:
            return <LogicNode> <object> frame.logic_node
        raise TooFewChildren()


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
        f'{self.op_repr}({ContextLogicExpression.c_safe_alias(self.left)}, {ContextLogicExpression.c_safe_alias(self.right)})'

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
        f'{self.op_repr}({ContextLogicExpression.c_safe_alias(self.left)}, {ContextLogicExpression.c_safe_alias(self.right)})'

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
        f'{self.op_repr}({ContextLogicExpression.c_safe_alias(self.left)}, {ContextLogicExpression.c_safe_alias(self.right)})'

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
