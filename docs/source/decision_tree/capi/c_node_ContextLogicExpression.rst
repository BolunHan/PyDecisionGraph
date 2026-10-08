c_node.ContextLogicExpression
=============================

.. py:module:: decision_graph.decision_tree.capi.c_node
   :no-index:

.. py:class:: ContextLogicExpression(LogicNode)

      Base class for expressions that evaluate against a logic group/context.

      This class implements Python operator overloads so expressions can be
      composed using normal Python syntax (e.g. a + b, a > b, a & b).

      :ivar logic_group: The LogicGroup used as the evaluation context.

   .. py:attribute:: logic_group

   .. py:attribute:: repr

   .. py:method:: __init__(self, *, logic_group: ~decision_graph.decision_tree.capi.c_abc.LogicGroup = None, **kwargs) -> None

      Create a context-aware expression.

      If ``logic_group`` is omitted the current active logic group is used.

      :param logic_group: Optional logic group or mapping providing contexts.
      :param \*\*kwargs: Implementation-specific keyword arguments.

   .. py:method:: __getitem__(self, key: str) -> ~decision_graph.decision_tree.capi.c_node.AttrExpression

      Return an attribute expression representing ``logic_group[key]``.

      :param key: Attribute/key name.

      :returns: Expression representing the attribute access.
      :rtype: AttrExpression

   .. py:method:: __getattr__(self, key: str) -> ~decision_graph.decision_tree.capi.c_node.AttrExpression

      Return an attribute expression representing ``logic_group.key``.

      :param key: Attribute name.

      :returns: Expression representing the attribute access.
      :rtype: AttrExpression

   .. py:method:: __add__(self, other: Any) -> MathExpression

   .. py:method:: __sub__(self, other: Any) -> MathExpression

   .. py:method:: __mul__(self, other: Any) -> MathExpression

   .. py:method:: __truediv__(self, other: Any) -> MathExpression

   .. py:method:: __floordiv__(self, other: Any) -> MathExpression

   .. py:method:: __pow__(self, other: Any) -> MathExpression

   .. py:method:: __neg__(self) -> MathExpression

   .. py:method:: __eq__(self, other: Any) -> ComparisonExpression

   .. py:method:: __ne__(self, other: Any) -> ComparisonExpression

   .. py:method:: __gt__(self, other: Any) -> ComparisonExpression

   .. py:method:: __ge__(self, other: Any) -> ComparisonExpression

   .. py:method:: __lt__(self, other: Any) -> ComparisonExpression

   .. py:method:: __le__(self, other: Any) -> ComparisonExpression

   .. py:method:: __and__(self, other: Any) -> LogicalExpression

   .. py:method:: __or__(self, other: Any) -> LogicalExpression

   .. py:method:: __invert__(self) -> LogicalExpression

