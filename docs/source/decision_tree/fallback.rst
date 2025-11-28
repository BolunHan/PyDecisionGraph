decision_tree fallback
==============================

This section documents the pure-Python fallback implementations for the decision tree API. These modules are provided as Python implementations (not C/Cython) and are intended primarily for compatibility and readability. They are slower than the native C/Cython implementations and may have small behavioral differences noted below.

Important differences from the native (C/Cython) implementation
---------------------------------------------------------------

- Performance: the fallback modules are implemented in pure Python and therefore have lower performance compared to the native C/Cython versions.

- Signature differences / behavioral notes:
  1. The `__eq__` operator on `LogicExpression` in the fallback returns a plain `bool` when compared, instead of producing a new `LogicExpression` as the native implementation does.
  2. The internal stacks for `LogicNode` and `LogicGroup` (managed by the LGM) are Python `list` objects in the fallback, not native C structures.
  3. Some base classes in the fallback do not include the Cython-style `**kwargs` guard used by the native implementation to silently discard extra cinit kwargs; that guard exists in the Cython types but not always in the Python fallback.

API reference (fallback modules)
----------------------------------------------

.. note:: These modules are included for API reference via Sphinx `autodoc` and are marked ``:noindex:`` so they don't compete with the Doxygen-generated API pages.

.. automodule:: decision_graph.decision_tree.native.abc
   :members:
   :undoc-members:
   :noindex:

.. automodule:: decision_graph.decision_tree.native.node
   :members:
   :undoc-members:
   :noindex:

.. automodule:: decision_graph.decision_tree.native.collection
   :members:
   :undoc-members:
   :noindex:
