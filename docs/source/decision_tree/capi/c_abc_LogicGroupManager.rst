LogicGroupManager
=================

.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: LogicGroupManager(Singleton)

      Singleton manager for LogicGroup instances and runtime expression context.

      Handles caching and reuse of ``LogicGroup`` objects and manages runtime
      stacks for active groups and nodes while building or evaluating decision
      graphs.

      Also supports shelving/unshelving state to create decision sub-graphs
      (for example, across function calls) without interfering with the main
      active state.

      :ivar inspection_mode: If True, generate layout without executing actions.
      :vartype inspection_mode: bool
      :ivar vigilant_mode: If True, perform stricter validation and avoid auto-generated nodes.
      :vartype vigilant_mode: bool

   .. py:attribute:: inspection_mode

   .. py:attribute:: vigilant_mode

   .. py:method:: __call__(self, name: str, cls: type[~decision_graph.decision_tree.capi.c_abc.LogicGroup], **kwargs) -> ~decision_graph.decision_tree.capi.c_abc.LogicGroup

      Get or create a cached LogicGroup instance with the given name.

      Useful for closed-loop operations that need to reuse the same logic group.

      :param name: The name of the logic group.
      :type name: str
      :param cls: The LogicGroup subclass to instantiate if not cached.
      :type cls: type[~decision_graph.decision_tree.capi.c_abc.LogicGroup]
      :param \*\*kwargs: Additional keyword arguments to pass to the LogicGroup constructor.

      :returns: The cached or newly created LogicGroup instance.
      :rtype: LogicGroup

   .. py:method:: __contains__(self, instance: ~decision_graph.decision_tree.capi.c_abc.LogicGroup) -> bool

      Return True if the given LogicGroup instance is cached by this manager.

   .. py:method:: shelve(self) -> None

      Shelve the current active group and node stacks and breakpoint stacks for later restoration.

      This is useful for creating isolated decision sub-graphs without interfering with the others graphs' state.

      With RootLogicNode contexts, the LGM automatically shelved and unshelved. See ``RootLogicNode`` for details.

   .. py:method:: unshelve(self) -> None

      Restore the most recently shelved active group and node stacks.

   .. py:method:: clear(self) -> None

      Clear all cached LogicGroup instances and reset runtime stacks.

   .. py:property:: active_group(self) -> ~decision_graph.decision_tree.capi.c_abc.LogicGroup | None

      The currently active LogicGroup, or None if no group context is entered.

   .. py:property:: active_node(self) -> ~decision_graph.decision_tree.capi.c_abc.LogicNode | None

      The currently active LogicNode expression, or None if no expression context is entered.
