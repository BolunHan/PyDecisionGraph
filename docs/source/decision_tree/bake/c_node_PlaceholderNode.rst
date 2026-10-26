PlaceholderNode
===============

.. py:module:: decision_graph.decision_tree.bake.c_node
   :no-index:

.. py:class:: PlaceholderNode(LogicNode)

      A reserved arm: where a branch goes if a build gives it one.

      A placeholder is the C layer's stand-in for a branch that has not been built
      yet, and it is never part of a baked graph. A branch that is built replaces
      it; one that never is gets a real auto-generated no-action in its place when
      the node around it is left, and the stand-in itself is released - this
      wrapper is a husk from that point on, and the slot holds a ``NoAction``.

   .. py:method:: __init__(self, **kwargs: Any) -> None

      Allocate a stand-in.

      :raises MemoryError: When the block cannot be allocated.
