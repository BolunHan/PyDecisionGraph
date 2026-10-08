c_collection.LogicMapping
=========================

.. py:module:: decision_graph.decision_tree.capi.c_collection
   :no-index:

.. py:class:: LogicMapping(LogicGroup)

      A mapping-like logic group that decouples stored data from the
      runtime context of a logic group.

      Instances behave like a read/write mapping container at the Python
      level; expressions created against a `LogicMapping` instance will
      consult the mapping stored in the group's contexts.

      :ivar data: The underlying dict object used to store key/value pairs.

   .. py:attribute:: data

   .. py:method:: __init__(self, *, name: str = None, data: dict[str, Any] = None, parent: Any | None = None, contexts: Optional[dict] = None) -> None

      Initialize the LogicMapping.

      :param name: Logical name for this group.
      :param data: Optional initial dict to use. If not a dict, it will be converted to a dict. If ``None``, the dict
                   will be taken from or created inside the group's ``contexts``
                   under the key ``'data'``.
      :param parent: Optional parent logic group (kept for parity with runtime
                     behaviour; type is intentionally elided here).
      :param contexts: Optional dict to use for the group's contexts. When not
                       provided, a default contexts mapping will be used/created by
                       the runtime manager.

   .. py:method:: __bool__(self) -> bool

      Return True when the underlying mapping is non-empty.

      :returns: True if the mapping contains at least one key; False otherwise.

   .. py:method:: __len__(self) -> int

      Return the number of items stored in the mapping.

      :returns: The number of key/value pairs in the underlying mapping.

   .. py:method:: __getitem__(self, key: str) -> ~decision_graph.decision_tree.capi.c_node.AttrExpression

      Return an expression representing the named key in this mapping.

      The return value is an expression object (implementation-specific)
      that, when evaluated, will fetch the value stored under ``key``.

      :param key: The mapping key.

      :returns: An AttrExpression that references ``key`` inside this mapping.

   .. py:method:: __getattr__(self, key: str) -> ~decision_graph.decision_tree.capi.c_node.AttrExpression

      Alias for ``__getitem__`` that allows attribute-style access.

      :param key: The attribute name to access.

      :returns: An AttrExpression that references ``key`` inside this mapping.

   .. py:method:: __contains__(self, key: str) -> bool

      Return True if ``key`` exists in the mapping.

   .. py:method:: update(self, *args: Any, **kwargs: Any) -> None

      Update the underlying mapping with the provided items.

      Accepts the same arguments as :meth:`dict.update`.

   .. py:method:: clear(self) -> None

      Remove all items from the underlying mapping.
