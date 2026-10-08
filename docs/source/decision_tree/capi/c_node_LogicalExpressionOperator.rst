c_node.LogicalExpressionOperator
================================

.. py:module:: decision_graph.decision_tree.capi.c_node
   :no-index:

.. py:class:: LogicalExpressionOperator(enum.StrEnum)

      Pseudo-enum class representing logical operators for LogicalExpression.

   .. py:attribute:: and_

   .. py:attribute:: or_

   .. py:attribute:: not_

   .. py:method:: to_func(self) -> UNARY_OP_FUNC | BINARY_OP_FUNC

   .. py:method:: from_str(cls, op_str: str) -> LogicalExpressionOperator
      :classmethod:

   .. py:property:: int_enum(self) -> int

.. py:module:: decision_graph.decision_tree.capi.c_node
   :no-index:

.. py:class:: LogicalExpression(ContextLogicExpression)

      Expression representing boolean logic operations.

      :ivar left: Left operand (expression or literal).
      :ivar right: Right operand or sentinel for unary operations.
      :ivar dtype: Resulting data type (bool).
      :ivar op_name: Name of the logical operator.
      :ivar op_repr: Operator symbol or representation.
      :ivar repr: Human-readable representation of the expression.

   .. py:attribute:: left

   .. py:attribute:: right

   .. py:attribute:: op_name

   .. py:attribute:: op_repr

   .. py:method:: __init__(self, *, left: Any, op: Any, right: Any = NO_DEFAULT, **kwargs) -> None

      Create a LogicalExpression.

      The constructor automatically passes the kwargs to underlying base classes, if any.
      - 'repr' is automatically generated based on the operator and operands, unless overridden via kwargs.
      - 'dtype' defaults to bool unless specified in kwargs.

      :param left: Left operand.
      :param op: Logical operator specifier (operator object, string, or callable).
      :param right: Optional right operand for binary operators.
      :param op_name: Optional operator name override.
      :param op_repr: Optional operator symbol override.

