AttrExpression
==============

.. py:module:: decision_graph.decision_tree.bake.c_collections
   :no-index:

.. py:class:: AttrExpression(VariableNode)

      The read of one entry in the store a build is inside.

      A variable node that names the group it reads, which is the whole of the
      difference between this and a bare variable: an entry is what it reads, and
      the store is what answers.

      A read begins holding WHERE its entry is - the entry's offset in the store,
      which no growth of the store can invalidate - and is resolved to the entry
      itself by its FIRST evaluation, which is where its type comes from. From then
      on the read holds that entry, and the values it reports are the entry's live
      ones for as long as they keep the type it was resolved to.

   .. py:method:: __init__(self, name: str) -> None

      Build the read of an entry in the active store.

      :param name: Entry name.

      :raises RuntimeError: When no group is active.
      :raises TypeError: When the active group is not a store.
      :raises KeyError: When the store is sealed and the entry is new.

   .. py:property:: value(self) -> Any

      The entry's value, read from the store of the moment.

      The value is read live either way: through the entry's offset while the
      read has not been evaluated, and through the entry itself once it has.
      None when the entry holds nothing yet.

   .. py:property:: logic_group(self) -> ~decision_graph.decision_tree.bake.c_collections.LogicMapping

      The store this read names - never None for a read.

      The base declares the field untyped (the node layer cannot name a group
      without an upward edge, DEPENDENCY.md 4.2); a read is only ever built
      against a store, which is what makes this precise rather than optional.
