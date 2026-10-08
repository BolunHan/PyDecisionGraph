c_collection.LogicGenerator
===========================

.. py:module:: decision_graph.decision_tree.capi.c_collection
   :no-index:

.. py:class:: LogicGenerator

      Wraps a generator/iterator to expose generator protocol operations
      via a logic-group object.

      This object is a thin wrapper around a Python iterator/generator.
      It exposes the standard generator protocol methods so callers can use
      ``next()``, ``send()``, ``throw()``, and ``close()`` directly on the
      logic generator instance.

      :ivar data: The underlying generator/iterator object.

   .. py:attribute:: data

   .. py:method:: __init__(self, *, name: str = None, data: Generator = None, parent: Any | None = None, contexts: Optional[dict] = None) -> None

      Initialize the LogicGenerator.

      :param name: Logical name for this group.
      :param data: Optional generator/iterator object. If ``None``, the implementation
                   may attempt to retrieve a generator from the group's
                   ``contexts`` mapping (behavior depends on the runtime).

   .. py:method:: __iter__(self) -> LogicGenerator

      Return self to support iteration protocol.

      :returns: The generator wrapper itself; calling ``iter()`` on the
                instance returns the same object so ``for x in instance:``
                works as expected.

   .. py:method:: __next__(self) -> Any

      Return the next value from the wrapped generator.

      :raises StopIteration: When the wrapped generator is exhausted.

   .. py:method:: send(self, value: Any) -> Any

      Send a value into the wrapped generator.

      Delegates to the underlying generator's :meth:`send` method. The
      original exceptions from the wrapped generator (such as
      :class:`AttributeError` when the underlying iterator does not
      implement ``send``) are allowed to propagate.

      :param value: The value to send into the generator.

      :returns: The next yielded value from the generator.

   .. py:method:: throw(self, typ: type[BaseException], val: Optional[BaseException] = None, tb: Optional[Any] = None) -> Any

      Raise an exception inside the wrapped generator.

      Delegates directly to the wrapped generator's :meth:`throw`.

      :param typ: Exception class to raise inside the generator.
      :param val: Optional exception instance or value.
      :param tb: Optional traceback object.

      :returns: The next yielded value from the generator (if any).

   .. py:method:: close(self) -> None

      Close the wrapped generator by delegating to its :meth:`close`.

      Any exceptions raised by the generator's close method are
      propagated unchanged.
