ShortAction
===========

.. py:module:: decision_graph.decision_tree.bake.c_action
   :no-index:

.. py:class:: ShortAction(ActionNode)

      The leaf that signals short.

   .. py:method:: __init__(self, *, sig: int = -1, repr: str = '~decision_graph.decision_tree.bake.c_action.ShortAction', auto_connect: bool = True, **kwargs: Any) -> None

      Build a short-action leaf.

      :param sig: The signal it carries, -1 by default.
      :param repr: Display text to copy.
      :param auto_connect: Whether it joins the node being built.

   .. py:method:: __int__(self) -> int

      The signal the leaf carries.
