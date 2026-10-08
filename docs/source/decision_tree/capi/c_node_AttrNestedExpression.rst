c_node.AttrNestedExpression
===========================

.. py:module:: decision_graph.decision_tree.capi.c_node
   :no-index:

.. py:class:: AttrNestedExpression(ContextLogicExpression)

      Expression representing nested attribute access (a.b.c).

      :ivar attrs: list of attribute path components in access order.

   .. py:attribute:: attrs

   .. py:method:: __init__(self, *, attrs: list[str], repr: str = None, **kwargs) -> None

      Create a nested attribute expression.

      The constructor automatically passes the kwargs to underlying base classes, if any.
      See ``ContextLogicExpression`` and ``LogicNode`` for more details.

      :param attrs: Sequence of attribute names describing the path.

   .. py:method:: __getitem__(self, key: str) -> AttrNestedExpression

   .. py:method:: __getattr__(self, key: str) -> AttrNestedExpression

