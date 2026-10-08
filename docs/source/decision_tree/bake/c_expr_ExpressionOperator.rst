ExpressionOperator
==================

.. py:module:: decision_graph.decision_tree.bake.c_expr
   :no-index:

.. py:class:: ExpressionOperator(enum.IntEnum)

      The op codes an expression node can carry.

      One member per code the C layer knows, family by family, with the arithmetic
      and comparison symbols spelled as the operators they are. The enum is the
      Python face of the C op codes: a cimported C constant is not a module
      attribute, so without it a caller would pass bare numbers.

   .. py:attribute:: none

   .. py:attribute:: arith

   .. py:attribute:: add

   .. py:attribute:: sub

   .. py:attribute:: mul

   .. py:attribute:: div

   .. py:attribute:: floordiv

   .. py:attribute:: pow

   .. py:attribute:: neg

   .. py:attribute:: compare

   .. py:attribute:: eq

   .. py:attribute:: ne

   .. py:attribute:: gt

   .. py:attribute:: ge

   .. py:attribute:: lt

   .. py:attribute:: le

   .. py:attribute:: logic

   .. py:attribute:: and_

   .. py:attribute:: or_

   .. py:attribute:: not_

   .. py:attribute:: access

   .. py:attribute:: attr

   .. py:attribute:: getitem
