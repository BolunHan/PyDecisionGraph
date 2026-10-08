ClearAction
===========

.. py:module:: decision_graph.decision_tree.bake.c_action
   :no-index:

.. py:class:: ClearAction(ActionNode)

      The leaf that flattens: it clears whatever position is held.

   .. py:method:: __init__(self, *, sig: int = 0, repr: str = '~decision_graph.decision_tree.bake.c_action.ClearAction', auto_connect: bool = True, **kwargs: Any) -> None

      Build a clear-action leaf.

      :param sig: The signal it carries, 0 by default.
      :param repr: Display text to copy.
      :param auto_connect: Whether it joins the node being built.

   .. py:method:: __int__(self) -> int

      The signal the leaf carries.
