c_node.GetterNestedExpression
=============================

.. py:module:: decision_graph.decision_tree.capi.c_node
   :no-index:

.. py:class:: GetterNestedExpression(ContextLogicExpression)

      Expression representing nested key/index access (a[b][c]).

      :ivar keys: list of keys/indexes in access order.

   .. py:attribute:: keys

   .. py:method:: __init__(self, *, keys: list[Any], repr: str = None, **kwargs) -> None

      Create a nested getter expression.

      The constructor automatically passes the kwargs to underlying base classes, if any.
      See ``ContextLogicExpression`` and ``LogicNode`` for more details.

      :param keys: Sequence of keys/indexes describing the access path.

   .. py:method:: __getitem__(self, key: Any) -> GetterNestedExpression

