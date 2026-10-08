CallExpression
==============

.. py:module:: decision_graph.decision_tree.bake.c_expr
   :no-index:

.. py:class:: CallExpression(ExpressionNode)

      An expression over any number of operands, written as a call.

      The callee's name is display text, composed into the node's ``repr``: it is
      not a field of the block, which is why a reconstruction brings a call back as
      the generic node view rather than as this class.

   .. py:method:: __init__(self, op: ExpressionOperator, inputs: Any, name: str | None = None) -> None

      Build a call-shaped expression.

      :param op: The operator to apply.
      :param inputs: The arguments, in order.
      :param name: The callee's name, for the node's display text.

      :raises TypeError: When an argument is not a node.
      :raises MemoryError: When the block cannot be allocated.
