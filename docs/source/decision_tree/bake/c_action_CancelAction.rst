CancelAction
============

.. py:module:: decision_graph.decision_tree.bake.c_action
   :no-index:

.. py:class:: CancelAction(ActionNode)

      The leaf that cancels an outstanding signal.

   .. py:method:: __init__(self, *, sig: int = 0, repr: str = '~decision_graph.decision_tree.bake.c_action.CancelAction', auto_connect: bool = True, **kwargs: Any) -> None

      Build a cancel-action leaf.

      :param sig: The signal it carries, 0 by default.
      :param repr: Display text to copy.
      :param auto_connect: Whether it joins the node being built.

   .. py:method:: __int__(self) -> int

      The signal the leaf carries.
