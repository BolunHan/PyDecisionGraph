c_abc.NodeEdgeCondition
========================

.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: NodeEdgeCondition(Singleton)

      Represents an edge condition in a decision graph.

      This is the base type for all concrete condition markers. Most users
      don't create these directly; instead, use the pre-created constants
      like ``TRUE_CONDITION``, ``FALSE_CONDITION``, ``ELSE_CONDITION``, or
      let conditions be inferred automatically when building graphs.

   .. py:property:: value(self) -> Any

.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: ConditionElse(NodeEdgeCondition)

      Represents an explicit "else" branch in decision trees.

      It matches when none of the other registered conditions match.


.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: ConditionAny(NodeEdgeCondition)

      Represents an unconditioned branch (always eligible as a fallback).


.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: ConditionAuto(NodeEdgeCondition)

      Marker used internally to request auto-inference of the edge condition.


.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: BinaryCondition(NodeEdgeCondition)

      Base type for binary True/False conditions.


.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: ConditionTrue(BinaryCondition)

      The boolean True branch condition.

      .. rubric:: Notes

      - Truthy when converted to ``bool``.
      - ``int(ConditionTrue)`` equals ``1``.
      - Unary negation or bitwise invert toggles to ``FALSE_CONDITION``.


.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: ConditionFalse(BinaryCondition)

      The boolean False branch condition.

      .. rubric:: Notes

      - Falsy when converted to ``bool``.
      - ``int(ConditionFalse)`` equals ``0``.
      - Unary negation or bitwise invert toggles to ``TRUE_CONDITION``.


