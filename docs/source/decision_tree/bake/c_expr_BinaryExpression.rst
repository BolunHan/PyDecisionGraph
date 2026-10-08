BinaryExpression
================

.. py:module:: decision_graph.decision_tree.bake.c_expr
   :no-index:

.. py:class:: BinaryExpression(ExpressionNode)

      An expression over two operands: the arithmetic and comparison shape.

   .. py:method:: __init__(self, op: ExpressionOperator, var_0: ~decision_graph.decision_tree.bake.c_node.LogicNode, var_1: ~decision_graph.decision_tree.bake.c_node.LogicNode) -> None

      Build a two-operand expression.

      :param op: The operator to apply.
      :param var_0: The left operand.
      :param var_1: The right operand.

      :raises MemoryError: When the block cannot be allocated.
