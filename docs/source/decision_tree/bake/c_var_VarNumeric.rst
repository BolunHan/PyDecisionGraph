VarNumeric
==========

.. py:module:: decision_graph.decision_tree.bake.c_var
   :no-index:

.. py:class:: VarNumeric(IntEnum)

      How a value reads as a number - the shape of the answer, not the value.

      A value that is not a number at all is ``none``, which is what the numeric
      operators refuse. The two that ARE numbers are what decides the type of an
      arithmetic result: an operation with a double in it is a double, and one
      without is whole.

   .. py:attribute:: none

   .. py:attribute:: int

   .. py:attribute:: double
