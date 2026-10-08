PlaceholderNode
===============

.. py:module:: decision_graph.decision_tree.bake.c_node
   :no-index:

.. py:class:: PlaceholderNode(LogicNode)

      A reserved arm: where a branch goes if a build gives it one.

      A placeholder is the C layer's stand-in for a branch that has not been built
      yet. A branch that is built replaces it, and one that never is becomes an
      auto-generated no-action when the node around it is left.

   .. py:method:: __init__(self, **kwargs: Any) -> None

      Allocate a stand-in.

      :raises MemoryError: When the block cannot be allocated.
