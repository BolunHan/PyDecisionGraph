c_node.AttrExpression
======================

.. py:module:: decision_graph.decision_tree.capi.c_node
   :no-index:

.. py:class:: AttrExpression(ContextLogicExpression)

      Expression representing a single attribute from the logic context.

      :ivar attr: The attribute name referred to by the expression.

   .. py:attribute:: attr

   .. py:method:: __init__(self, *, attr: str, repr: str = None, **kwargs) -> None

      Create an attribute expression.

      The constructor automatically passes the kwargs to underlying base classes, if any.
      See ``ContextLogicExpression`` and ``LogicNode`` for more details.

      :param attr: Attribute name or list of nested attribute names.
      :param repr: Optional textual representation override.

   .. py:method:: __getitem__(self, key: str) -> AttrNestedExpression

   .. py:method:: __getattr__(self, key: str) -> AttrNestedExpression

