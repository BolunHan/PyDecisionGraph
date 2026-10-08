VarType
=======

.. py:module:: decision_graph.decision_tree.bake.c_var
   :no-index:

.. py:class:: VarType(IntEnum)

      The tag of a value: what it is, and whether it is a reference.

      The members are the C enumerators by value, so a tag crosses the boundary as
      itself and no translation table exists to fall out of step. The reference
      ones are named as the C ones are - ``double_ref`` refers to a double,
      ``double_ref_ref`` to a reference to one, one name per rung - and
      ``inferred`` is a reference to a slot whose type is not known yet.

      :ivar node: A node, held as its own address. It is what an action leaf's slot
                  carries - the node IS the value it stands for - and what unpacks back
                  into that node's wrapper.
      :ivar reserved: A slot holding nothing yet: an entry whose value has not
                      arrived. ``is_null`` is true for one, and reading it refuses.
      :ivar inferred: A reference to a ``reserved`` slot - "the type is the slot's to
                      say". It is what a read of a not-yet-filled store entry is born as.

   .. py:attribute:: raw_ptr

   .. py:attribute:: string

   .. py:attribute:: bool

   .. py:attribute:: double

   .. py:attribute:: int

   .. py:attribute:: offset

   .. py:attribute:: time

   .. py:attribute:: date

   .. py:attribute:: datetime

   .. py:attribute:: d_vector

   .. py:attribute:: d_matrix

   .. py:attribute:: reserved

   .. py:attribute:: node

   .. py:attribute:: raw_ptr_ref

   .. py:attribute:: string_ref

   .. py:attribute:: bool_ref

   .. py:attribute:: double_ref

   .. py:attribute:: int_ref

   .. py:attribute:: offset_ref

   .. py:attribute:: time_ref

   .. py:attribute:: date_ref

   .. py:attribute:: datetime_ref

   .. py:attribute:: d_vector_ref

   .. py:attribute:: d_matrix_ref

   .. py:attribute:: node_ref

   .. py:attribute:: inferred

   .. py:attribute:: raw_ptr_ref_ref

   .. py:attribute:: string_ref_ref

   .. py:attribute:: bool_ref_ref

   .. py:attribute:: double_ref_ref

   .. py:attribute:: int_ref_ref

   .. py:attribute:: offset_ref_ref

   .. py:attribute:: time_ref_ref

   .. py:attribute:: date_ref_ref

   .. py:attribute:: datetime_ref_ref

   .. py:attribute:: d_vector_ref_ref

   .. py:attribute:: d_matrix_ref_ref

   .. py:attribute:: node_ref_ref
