ActionNode
==========

.. py:module:: decision_graph.decision_tree.bake.c_action
   :no-index:

.. py:class:: ActionNode(LogicNode)

      An action leaf: a typed terminal node carrying a signal.

      The class itself is the family's own; a caller builds the named leaves below
      it, which differ only in the type and the signal they pass.

   .. py:method:: __init__(self, node_type: int, *, repr: str | None = None, sig: int = 0, auto_connect: bool = True, **kwargs: Any) -> None

      Build an action of an explicit node type.

      :param node_type: The C node type this action is.
      :param repr: Display text to copy; the type's own text when not given.
      :param sig: The signal it carries: +1 long, -1 short, 0 for the rest.
      :param auto_connect: Whether it joins the node being built.

      :raises MemoryError: When the block cannot be allocated.
