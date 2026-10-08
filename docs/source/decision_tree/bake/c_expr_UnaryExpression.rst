UnaryExpression
===============

.. py:module:: decision_graph.decision_tree.bake.c_expr
   :no-index:

.. py:class:: UnaryExpression(ExpressionNode)

      An expression over one operand.

   .. py:method:: __init__(self, op: ExpressionOperator, src: ~decision_graph.decision_tree.bake.c_node.LogicNode) -> None

      Build a one-operand expression.

      :param op: The operator to apply.
      :param src: The operand it applies to.

      :raises MemoryError: When the block cannot be allocated.
