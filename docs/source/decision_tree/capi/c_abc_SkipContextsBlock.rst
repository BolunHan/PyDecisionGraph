c_abc.SkipContextsBlock
=======================

.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: SkipContextsBlock

      Context manager that may skip executing the body of a with-block.

      - If the entry check passes, ``__enter__`` returns ``self`` and normal
        execution proceeds until ``__exit__`` is called.
      - If the entry check fails, execution of the block is prevented via
        tracing hooks and an internal control-flow exception; ``__exit__`` then
        suppresses that exception so the program continues after the block.

      :ivar default_entry_check: If True, the block executes by default.
      :vartype default_entry_check: bool

   .. py:attribute:: default_entry_check

   .. py:method:: __enter__(self) -> SkipContextsBlock

   .. py:method:: __exit__(self, exc_type: type[BaseException] | None, exc_value: BaseException | None, exc_traceback) -> bool | None

