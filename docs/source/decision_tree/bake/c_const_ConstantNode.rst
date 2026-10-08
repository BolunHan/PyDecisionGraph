ConstantNode
============

.. py:module:: decision_graph.decision_tree.bake.c_const
   :no-index:

.. py:class:: ConstantNode(LogicNode)

      A literal input: the value it stands for is in the node.

      The C type follows the value - a bool is a true or a false node, an int an
      int, a float a double, a str a string - so there is nothing for a caller to
      say that the value does not already say.

   .. py:method:: __init__(self, value: Any, *, repr: str | None = None) -> None

      Build a literal for a Python value.

      :param value: The literal: a bool, an int, a float or a str.
      :param repr: Display text to copy; the value's own text when not given.

      :raises TypeError: When the value has no node type - a container, None.
      :raises MemoryError: When the block cannot be allocated.

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

      Compose a negation of this literal.

   .. py:method:: __eq__(self, other: ~decision_graph.decision_tree.bake.c_node.LogicNode | bool | int | float | str) -> BinaryExpression

      Compose an equality CONDITION with another node.

      Comparing two nodes builds the comparison node, as it does in the capi -
      the result is a branch, not a bool.

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

      Compose the negation of this literal as a condition.

   .. py:property:: value(self) -> Any

      The value the literal holds, read by the value's own tag.
