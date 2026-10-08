TernaryExpression
=================

.. py:module:: decision_graph.decision_tree.bake.c_expr
   :no-index:

.. py:class:: TernaryExpression(ExpressionNode)

      An expression over three operands: the conditional shape.

   .. py:method:: __init__(self, op: ExpressionOperator, var_0: ~decision_graph.decision_tree.bake.c_node.LogicNode, var_1: ~decision_graph.decision_tree.bake.c_node.LogicNode, var_2: ~decision_graph.decision_tree.bake.c_node.LogicNode) -> None

      Build a three-operand expression.

      :param op: The operator to apply.
      :param var_0: The first operand.
      :param var_1: The second operand.
      :param var_2: The third operand.

      :raises MemoryError: When the block cannot be allocated.
