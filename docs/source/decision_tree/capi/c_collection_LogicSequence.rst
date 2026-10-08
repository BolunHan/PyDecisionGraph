c_collection.LogicSequence
==========================

.. py:module:: decision_graph.decision_tree.capi.c_collection
   :no-index:

.. py:class:: LogicSequence(LogicGroup)

      A sequence-like logic group that exposes an ordered collection to
      logic expressions.

      This class mimics the behaviour of Python sequences at the API layer
      so that expressions can index into it (e.g. ``seq[0]``) and tools can
      iterate over it to produce expression entries.

      :ivar data: The underlying list object used to store items.

   .. py:attribute:: data

   .. py:method:: __init__(self, *, name: str = None, data: list[Any] = None, parent: Any | None = None, contexts: Optional[dict] = None) -> None

      Initialize the LogicSequence.

      :param name: Logical name for this group.
      :param data: Optional initial list to use. If not a list, it will be converted to a list. If ``None``, the list
                   will be taken from or created inside the group's ``contexts``
                   under the key ``'data'``.
      :param parent: Optional parent logic group (opaque in this stub).
      :param contexts: Optional contexts mapping used by the runtime manager.

   .. py:method:: __iter__(self) -> Iterator[GetterExpression]

      Yields GetterExpression objects representing each element in the sequence.

      :Yields: GetterExpression objects (index-based) that can evaluate to the
               underlying sequence items.

   .. py:method:: __len__(self) -> int

      Return the length of the underlying sequence.

   .. py:method:: __getitem__(self, index: int) -> GetterExpression

      Return an expression referencing the element at ``index``.

      Negative indices follow Python semantics if the underlying
      sequence supports them.

      :param index: The index to access.

      :returns: A GetterExpression that, when evaluated, returns the item at
                the requested index.

   .. py:method:: __contains__(self, item: Any) -> bool

      Return True if ``item`` is present in the underlying sequence.

   .. py:method:: append(self, value: Any) -> None

      Append ``value`` to the underlying sequence.

   .. py:method:: extend(self, iterable: Sequence[Any]) -> None

      Extend the underlying sequence by the items from ``iterable``.

   .. py:method:: insert(self, index: int, value: Any) -> None

      Insert ``value`` at position ``index`` in the underlying sequence.

   .. py:method:: remove(self, value: Any) -> None

      Remove the first occurrence of ``value`` from the underlying sequence.

   .. py:method:: pop(self, index: int = -1) -> Any

      Remove and return item at ``index`` (default last).

      :param index: Position of item to pop; default -1 (last item).

      :returns: The removed item.

   .. py:method:: clear(self) -> None

      Remove all items from the underlying sequence.

   .. py:method:: __bool__(self) -> bool

      Return True when the underlying sequence is non-empty.
