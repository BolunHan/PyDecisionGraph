NoAction
========

.. py:module:: decision_graph.decision_tree.bake.c_action
   :no-index:

.. py:class:: NoAction(ActionNode)

      The leaf that decides nothing: what an unfilled arm closes as.

   .. py:method:: __init__(self, *, sig: int = 0, repr: str = '~decision_graph.decision_tree.bake.c_action.NoAction', auto_connect: bool = True, autogen: bool = False, **kwargs: Any) -> None

      Build a no-action leaf.

      :param sig: The signal it carries, 0 by default.
      :param repr: Display text to copy.
      :param auto_connect: Whether it joins the node being built.
      :param autogen: Whether the builder generated it rather than a caller.

   .. py:method:: __int__(self) -> int

      The signal the leaf carries.
