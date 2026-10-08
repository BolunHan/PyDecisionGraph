LogicExpression
==========================

.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: LogicExpression(SkipContextsBlock)

      Represents a logical or mathematical expression with deferred eval.

      The expression can be a static value, an exception to raise on evaluation,
      or a callable returning a value. An optional ``dtype`` can be provided to
      enforce or check the evaluated type.

      This class supports boolean logic (``&``, ``|``, comparisons) and basic
      arithmetic operators that produce new ``LogicExpression`` instances.

      :ivar expression: The underlying expression (value, exception, or callable).
      :vartype expression: object
      :ivar dtype: Optional type to enforce on evaluation, if requested.
      :vartype dtype: type | None
      :ivar repr: String representation for debugging and logging.
      :vartype repr: str
      :ivar uid: Unique identifier for the LogicExpression instance.
      :vartype uid: uuid.UUID

   .. py:attribute:: expression

   .. py:attribute:: dtype

   .. py:attribute:: repr

   .. py:attribute:: uid

   .. py:method:: __init__(self, *, expression: float | int | bool | Exception | Callable[[], Any] = None, dtype: type = None, repr: str = None, uid: uuid.UUID = None, **kwargs) -> None

      Initialize the LogicExpression.

      :param expression: A callable or static value.
      :type expression: Union[Any, Callable[[], Any]]
      :param dtype: The expected type of the evaluated value (e.g. float, int, or bool).
      :type dtype: type, optional
      :param repr: A string representation of the expression.
      :type repr: str, optional
      :param uid: Unique identifier for the expression. If None, a new UUID is generated.
      :type uid: uuid.UUID, optional
      :param kwargs: __cinit__ extra kwargs guardian of for subclassing support, not used is this base class.

   .. py:method:: eval(self, enforce_dtype: bool = False) -> Any

      Evaluate the expression and return the resulting value.

      If ``enforce_dtype`` is True and a ``dtype`` was provided, the result
      is cast using ``self.dtype(value)``.

   .. py:method:: cast(cls, value: int | float | bool | Exception | LogicExpression | Callable[[], Any], dtype: type = None) -> LogicExpression
      :classmethod:

      Cast a value into a LogicExpression.
      If the value is already a LogicExpression, it is returned as-is (same instance).

      :returns: The resulting LogicExpression instance.
      :rtype: LogicExpression

      :raises TypeError: If the value type is unsupported.

   .. py:method:: __bool__(self) -> bool

      Evaluate the expression and return its boolean value.

   .. py:method:: __and__(self, other: LogicExpression | bool) -> LogicExpression

      Return a new LogicExpression representing logical AND with ``other``.

   .. py:method:: __eq__(self, other: object) -> LogicExpression

      Return a new LogicExpression representing equality comparison to ``other``.

   .. py:method:: __or__(self, other: LogicExpression | bool) -> LogicExpression

      Return a new LogicExpression representing logical OR with ``other``.

   .. py:method:: __add__(self, other: object) -> LogicExpression

      Return a new LogicExpression representing addition with ``other``.

   .. py:method:: __sub__(self, other: object) -> LogicExpression

      Return a new LogicExpression representing subtraction with ``other``.

   .. py:method:: __mul__(self, other: object) -> LogicExpression

      Return a new LogicExpression representing multiplication with ``other``.

   .. py:method:: __truediv__(self, other: object) -> LogicExpression

      Return a new LogicExpression representing true division with ``other``.

   .. py:method:: __floordiv__(self, other: object) -> LogicExpression

      Return a new LogicExpression representing floor division with ``other``.

   .. py:method:: __pow__(self, other: object) -> LogicExpression

      Return a new LogicExpression representing exponentiation with ``other``.

   .. py:method:: __lt__(self, other: object) -> LogicExpression

      Return a new LogicExpression representing less-than comparison to ``other``.

   .. py:method:: __le__(self, other: object) -> LogicExpression

      Return a new LogicExpression representing less-than-or-equal comparison to ``other``.

   .. py:method:: __gt__(self, other: object) -> LogicExpression

      Return a new LogicExpression representing greater-than comparison to ``other``.

   .. py:method:: __ge__(self, other: object) -> LogicExpression

      Return a new LogicExpression representing greater-than-or-equal comparison to ``other``.

   .. py:method:: __repr__(self) -> str

      Return the string representation of the LogicExpression.
