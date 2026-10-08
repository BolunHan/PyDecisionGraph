VariableNode
============

.. py:module:: decision_graph.decision_tree.bake.c_const
   :no-index:

.. py:class:: VariableNode(LogicNode)

      A variable input: a node that reads a value rather than holding one.

      It holds no value of its own: what it reads is the slot it was bound to, and
      the read is live - whatever that slot holds when the node is read is what the
      node reports. A node built without a slot reflects nothing until one is
      given.

   .. py:method:: __init__(self, *, key: str | None = None, repr: str | None = None) -> None

      Build a read of an entry.

      :param key: Entry name in the group it is built inside, if any.
      :param repr: Display text to copy.

      :raises MemoryError: When the block cannot be allocated.

   .. py:method:: c_bind_const(self, node: ConstantNode) -> None

      Read a literal's slot, so the read follows the value it holds.

      :param node: The literal whose value this read reflects.

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

      Compose a negation of this read.

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

      Compose the negation of this read as a condition.

   .. py:property:: value(self) -> Any

      The value the read stands for, read through its binding.

   .. py:property:: key(self) -> str | None

      The store entry this read names, or None when it names none.

   .. py:property:: logic_group(self) -> ~decision_graph.decision_tree.bake.c_logic_group.LogicGroup | None

      The group this read belongs to, or None for a standalone node.

      The C field is an untyped pointer because the node layer cannot name a
      group without an upward edge (DEPENDENCY.md 4.2); what it holds is the
      group the read was built inside, which is a store's entry in the case
      that matters - see ``c_collections.AttrExpression``.
