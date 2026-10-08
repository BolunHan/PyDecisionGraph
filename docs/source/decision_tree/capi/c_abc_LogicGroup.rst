c_abc.LogicGroup
=================

.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: LogicGroup

      A minimal context manager to scope logic groups and break operations.

      A logic group is a lightweight context that records its name and an
      optional parent, and it provides a ``Break`` exception type for orderly
      early exit via ``LogicGroup.break_``.

      In runtime mode, breaking from a logic group propagates through nested
      groups and moves the execution cursor to the first line after the block;
      during this process, on-exit hooks of nested groups and nodes run.

      In inspection mode, breaking does not raise immediately. Instead, missing
      branches are auto-filled with ``NoAction`` to allow layout evaluation to
      continue, especially when a break occurs before the other branch is built.

      :ivar name: The name of the logic group.
      :vartype name: str
      :ivar parent: The parent logic group, if any.
      :vartype parent: ~decision_graph.decision_tree.capi.c_abc.LogicGroup | None
      :ivar Break: The exception type used for breaks.
      :vartype Break: type[BaseException]
      :ivar contexts: Context-specific storage for the group.
      :vartype contexts: dict[str, Any]

   .. py:attribute:: name

   .. py:attribute:: parent

   .. py:attribute:: Break

   .. py:attribute:: contexts

   .. py:method:: __init__(self, *, name: str = None, parent: ~decision_graph.decision_tree.capi.c_abc.LogicGroup = None, contexts: dict = None, **kwargs)

      Initialize a LogicGroup with the given name, parent, and contexts.

      :param name: The name of the logic group. If None, a unique name is assigned.
      :type name: str
      :param parent: The parent logic group, if any.
      :type parent: ~decision_graph.decision_tree.capi.c_abc.LogicGroup | None
      :param contexts: Optional context-specific storage.
      :type contexts: dict[str, Any] | None
      :param kwargs: __cinit__ extra kwargs guardian of for subclassing support, not used is this base class.

   .. py:method:: __repr__(self) -> str

   .. py:method:: __enter__(self) -> ~decision_graph.decision_tree.capi.c_abc.LogicGroup

      Enter the logic group context and mark it as active.

   .. py:method:: __exit__(self, exc_type: type[BaseException] | None, exc_value: BaseException | None, exc_traceback) -> bool | None

      Exit the logic group context, handling Break exceptions gracefully.

   .. py:method:: break_(cls, scope: ~decision_graph.decision_tree.capi.c_abc.LogicGroup = None) -> None
      :classmethod:

      Break out from the given ``scope`` (or the active scope if None).

      In inspection mode, the break is recorded to be connected to the next
      entered node. In runtime mode, this propagates break through nested
      groups until the target scope is exited.

   .. py:method:: break_active(self) -> None

      Break out from the currently active logic group only (top of stack).

   .. py:method:: break_inspection(self) -> None

      Record a break while in inspection mode without raising immediately.

   .. py:method:: break_runtime(self) -> None

      Propagate a break through nested groups until the target scope is exited.
