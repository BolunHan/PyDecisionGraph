NodeEdgeCondition
=================

.. py:module:: decision_graph.decision_tree.bake.c_edge
   :no-index:

.. py:class:: NodeEdgeCondition

      A condition an edge carries, as a tagged value.

      An instance of this class is a **user condition**: it holds a value, and a
      parent takes the edge when its evaluated result matches that value. The five
      built-ins are instances of the subclasses below and are module constants.

      :ivar _header: The C condition block the wrapper stands for.
      :ivar _owner: Whether this wrapper may free that block.

   .. py:method:: __init__(self, py_value: Any = ..., repr: str | None = None) -> None

      Wrap a Python value as an edge condition.

      :param py_value: The value the parent's result is compared against.
      :param repr: Display text for the condition.

      :raises ValueError: When no value is given - a user condition needs one.

   .. py:method:: __hash__(self) -> int

      Hash by the C block's address, or by the type for a built-in.

   .. py:method:: __eq__(self, other: ~decision_graph.decision_tree.bake.c_edge.NodeEdgeCondition) -> bool

      Compare as edges: the tag first, then the payload.

      :param other: Condition to compare against.

      :returns: True when both conditions are the same edge.

   .. py:method:: __ne__(self, other: ~decision_graph.decision_tree.bake.c_edge.NodeEdgeCondition) -> bool

      The negation of ``__eq__``.

   .. py:method:: __repr__(self) -> str

      The condition's display text, class name included.

   .. py:method:: __str__(self) -> str

      The condition's display text.

   .. py:property:: address(self) -> int

      The block this condition is, as the edge registry keys it.

   .. py:property:: is_none(self) -> bool

      Whether this is the unconditional edge.

   .. py:property:: is_else(self) -> bool

      Whether this is the fallback edge.

   .. py:property:: is_auto(self) -> bool

      Whether this edge's arm is for the parent to infer.

   .. py:property:: is_binary(self) -> bool

      Whether this is a two-way edge: the true or the false arm.

   .. py:property:: value(self) -> Any

      The value this edge is taken by: what the parent's result must match.

      A caller's condition carries the value it was built with, and it is read
      here as the Python object it was. The three edges that stand for "no
      particular value" - the unconditional, the else and the auto - hold none,
      so reading one raises rather than answering with a value that says
      nothing. The two arms of a branch are the exception the other way: they
      DO answer, with the ``True`` and the ``False`` they are, which is what
      makes a built-in edge usable without unpacking the C tag first.

      :raises ValueError: When the condition holds no value.
      :raises RuntimeError: When the wrapper is uninitialised.
