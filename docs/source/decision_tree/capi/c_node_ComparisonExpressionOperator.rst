c_node.ComparisonExpressionOperator
===================================

.. py:module:: decision_graph.decision_tree.capi.c_node
   :no-index:

.. py:class:: ComparisonExpressionOperator(enum.StrEnum)

      A pseudo-enum class representing comparison operators for ComparisonExpression.

   .. py:attribute:: eq

   .. py:attribute:: ne

   .. py:attribute:: gt

   .. py:attribute:: ge

   .. py:attribute:: lt

   .. py:attribute:: le

   .. py:method:: to_func(self) -> BINARY_OP_FUNC

   .. py:method:: from_str(cls, op_str: str) -> ComparisonExpressionOperator
      :classmethod:

   .. py:property:: int_enum(self) -> int

.. py:module:: decision_graph.decision_tree.capi.c_node
   :no-index:

.. py:class:: ComparisonExpression(ContextLogicExpression)

      Expression representing a comparison operation, returning only boolean result.

   .. py:attribute:: left

   .. py:attribute:: right

   .. py:attribute:: op_name

   .. py:attribute:: op_repr

   .. py:method:: __init__(self, *, left: Any, op: Any, right: Any, **kwargs) -> None

      Create a ComparisonExpression.

      The constructor automatically passes the kwargs to underlying base classes, if any.
      - 'repr' is automatically generated based on the operator and operands, unless overridden via kwargs.
      - 'dtype' defaults to bool unless specified in kwargs.

      :param left: Left operand.
      :param op: Operator specifier (operator object, string, or callable).
      :param right: Right operand.
      :param op_name: Optional operator name override.
      :param op_repr: Optional operator symbol override.

