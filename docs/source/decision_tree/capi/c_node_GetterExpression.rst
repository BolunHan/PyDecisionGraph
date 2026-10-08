c_node.GetterExpression
========================

.. py:module:: decision_graph.decision_tree.capi.c_node
   :no-index:

.. py:class:: GetterExpression(ContextLogicExpression)

      Expression representing a single key/index access from the logic context.

      :ivar key: The key/index referred to by the expression.

   .. py:attribute:: key

   .. py:method:: __init__(self, *, key: Any, repr: str = None, **kwargs) -> None

      Create a getter expression.

      The constructor automatically passes the kwargs to underlying base classes, if any.
      See ``ContextLogicExpression`` and ``LogicNode`` for more details.

      :param key: Key/index or list of nested keys/indexes.
      :param repr: Optional textual representation override.

   .. py:method:: __getitem__(self, key: Any) -> GetterNestedExpression

