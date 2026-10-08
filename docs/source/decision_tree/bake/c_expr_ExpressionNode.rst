ExpressionNode
==============

.. py:module:: decision_graph.decision_tree.bake.c_expr
   :no-index:

.. py:class:: ExpressionNode(LogicNode)

      An operator over operands, of an arity the type does not fix.

      The operands are the nodes its arguments read from, and this wrapper HOLDS
      them: an argument that refers to a node reads that node's storage, so a
      released operand would leave the expression reading a block that is gone.

   .. py:method:: __init__(self, n_args: int = ..., node_type: int = ...) -> None

      Build an expression of an explicit arity and node type.

      :param n_args: How many operands it takes.
      :param node_type: The C node type this expression is.

      :raises MemoryError: When the block cannot be allocated.

   .. py:method:: bind(self, index: int, node: ~decision_graph.decision_tree.bake.c_node.LogicNode) -> None

      Bind an operand to one of this expression's argument slots.

      :param index: Argument position to bind.
      :param node: Node whose value the argument takes.

      :raises RuntimeError: When the C layer refuses the binding.

   .. py:method:: __add__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a sum expression with another node.

   .. py:method:: __radd__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a sum expression with a Python value on the left.

   .. py:method:: __sub__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a difference expression with another node.

   .. py:method:: __rsub__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a difference expression with a Python value on the left.

   .. py:method:: __mul__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a product expression with another node.

   .. py:method:: __rmul__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a product expression with a Python value on the left.

   .. py:method:: __truediv__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a division expression with another node.

   .. py:method:: __rtruediv__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a division expression with a Python value on the left.

   .. py:method:: __floordiv__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a floor-division expression with another node.

   .. py:method:: __rfloordiv__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a floor-division expression with a Python value on the left.

   .. py:method:: __pow__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a power expression with another node.

   .. py:method:: __rpow__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a power expression with a Python value on the left.

   .. py:method:: __neg__(self) -> UnaryExpression

      Compose a negation of this expression.

   .. py:method:: __eq__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose an equality CONDITION with another node.

   .. py:method:: __ne__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose an inequality condition with another node.

   .. py:method:: __hash__(self) -> int

      Hash by the C block's address, so a node can key a dict.

   .. py:method:: __lt__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a less-than condition with another node.

   .. py:method:: __le__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a less-or-equal condition with another node.

   .. py:method:: __gt__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a greater-than condition with another node.

   .. py:method:: __ge__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a greater-or-equal condition with another node.

   .. py:method:: __and__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a conjunction with another node.

   .. py:method:: __rand__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a conjunction with a Python value on the left.

   .. py:method:: __or__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a disjunction with another node.

   .. py:method:: __ror__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose a disjunction with a Python value on the left.

   .. py:method:: __invert__(self) -> UnaryExpression

      Compose the negation of this expression as a condition.

   .. py:property:: op(self) -> ExpressionOperator

      The operator this expression applies.

   .. py:property:: n_args(self) -> int

      How many operands the expression takes.

   .. py:property:: operands(self) -> list[~decision_graph.decision_tree.bake.c_node.LogicNode | None]

      The nodes the arguments read from, in argument order.

      An entry is None where the argument reads from nothing this wrapper
      holds - an argument a value was folded into, whose operand node is gone
      by design.
