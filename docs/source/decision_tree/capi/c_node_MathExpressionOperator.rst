c_node.MathExpressionOperator
=============================

.. py:module:: decision_graph.decision_tree.capi.c_node
   :no-index:

.. py:class:: MathExpressionOperator(enum.StrEnum)

      A pseudo-enum class representing mathematical operators for MathExpression.

   .. py:attribute:: add

   .. py:attribute:: sub

   .. py:attribute:: mul

   .. py:attribute:: truediv

   .. py:attribute:: floordiv

   .. py:attribute:: pow

   .. py:attribute:: neg

   .. py:method:: to_func(self) -> UNARY_OP_FUNC | BINARY_OP_FUNC

   .. py:method:: from_str(cls, op_str: str) -> MathExpressionOperator
      :classmethod:

.. py:module:: decision_graph.decision_tree.capi.c_node
   :no-index:

.. py:class:: MathExpression(ContextLogicExpression)

      Expression representing an arithmetic operation.

      :ivar left: Left operand (expression or literal).
      :ivar right: Right operand or sentinel when unary.
      :ivar dtype: Resulting data type (defaults to float).
      :ivar op_name: Internal operator name.
      :ivar op_repr: Human-readable operator symbol or function name.

   .. py:attribute:: left

   .. py:attribute:: right

   .. py:attribute:: op_name

   .. py:attribute:: op_repr

   .. py:method:: __init__(self, *, left: Any, op: str | MathExpressionOperator | UNARY_OP_FUNC | BINARY_OP_FUNC, right: Any = NO_DEFAULT, **kwargs) -> None

      Create a MathExpression.

      The constructor automatically passes the kwargs to underlying base classes, if any.
      - `repr` is automatically generated based on the operator and operands, unless overridden via kwargs.
      - `dtype` defaults to float unless specified in kwargs.

      :param left: Left operand, can be an expression or a literal.
      :param op: Operator specifier (operator object, string, or callable).
      :param right: Right operand or omitted for unary operators.
      :param op_name: Optional operator name override.
      :param op_repr: Optional operator symbol override.

