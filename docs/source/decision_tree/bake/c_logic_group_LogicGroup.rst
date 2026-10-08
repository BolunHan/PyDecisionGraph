LogicGroup
==========

.. py:module:: decision_graph.decision_tree.bake.c_logic_group
   :no-index:

.. py:class:: LogicGroup

      A scope a graph is built inside.

      A group carries a name, a type and a parent, and a build entered into it is
      inside it - `with group:` enters, and leaving restores whatever was active
      before.

   .. py:method:: __init__(self, *, name: str | None = None, parent: ~decision_graph.decision_tree.bake.c_logic_group.LogicGroup | None = None, **kwargs: Any) -> None

      Build a group of the base type.

      :param name: The group's name.
      :param parent: The group it is built inside, if any.

      :raises MemoryError: When the block cannot be allocated.

   .. py:method:: __repr__(self) -> str

      The group's own text: its class and its name.

   .. py:method:: __enter__(self) -> Self

      Enter the group as the scope of the build inside it.

      :returns: This group.

   .. py:method:: __exit__(self, exc_type: Any, exc_value: Any, traceback: Any) -> bool

      Leave the group, restoring what was active before it.

      :returns: False, so an exception raised inside the block keeps travelling.

   .. py:method:: break_(self, scope: ~decision_graph.decision_tree.bake.c_logic_group.LogicGroup | None = None) -> None

      Raise an inspection breakpoint inside this group.

      :param scope: The group to break out of; this one when not given.

   .. py:method:: break_active(cls, scope: ~decision_graph.decision_tree.bake.c_logic_group.LogicGroup | None = None) -> None
      :classmethod:

      Raise an inspection breakpoint in the build that is running.

      :param scope: The group to break out of.

   .. py:property:: name(self) -> str | None

      The group's name, or None for a group that has none.

   .. py:property:: parent(self) -> ~decision_graph.decision_tree.bake.c_logic_group.LogicGroup | None

      The group this one was built inside, or None.

   .. py:property:: lgtype(self) -> int

      The group's C type, as the enumerator's value.

   .. py:property:: address(self) -> int

      The group's C block, which is what the registry is keyed by.
