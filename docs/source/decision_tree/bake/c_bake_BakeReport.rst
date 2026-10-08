BakeReport
==========

.. py:module:: decision_graph.decision_tree.bake.c_bake
   :no-index:

.. py:class:: BakeReport

      What a bake found, and what it affected.

      A wrapper over one ``dcg_bake_report`` block, which it either OWNS - a report
      of its own, the one a bake is asked to fill - or merely reads through, when a
      caller made the block itself. A report owns nothing behind its block: the node
      it may name belongs to the graph, so the block going is the whole of its
      release.

      Its fields read where the pass wrote them, so asking twice answers the same
      thing; and a report of its own that no bake has filled yet is a report of a
      bake that never ran - ``DCG_OK``, no node, no counts.

      :ivar code: The ``DCG_ERR_*`` code the pass ended with - ``DCG_OK`` when the
                  graph is baked.
      :ivar code_name: That code's stable name (``'OK'``, ``'TYPE'``, ``'UNBOUND'``).
      :ivar node: The wrapper of the node the first problem was found on, or None
                  when the failure was the pass's own (a machine that ran out of room).
      :ivar errors: How many problems were found.
      :ivar nodes: How many nodes the pass walked - the branches, and the operands
                   they read.
      :ivar depth: The deepest level the walk reached.
      :ivar locked: How many nodes this pass locked; zero on a re-bake, and zero for
                    a validate-only bake.
      :ivar sealed: How many stores this pass sealed.
      :ivar capacity: The entries the root's record was prepared with; zero when the
                      pass did not prepare one.

   .. py:method:: __init__(self, address: int = 0, owner: bool = False) -> None

      A report of its own, or a window onto one that is somebody else's.

      :param address: Address of a ``dcg_bake_report`` to read through, for the
                      caller that made the block itself; 0, the default, makes a report
                      of its own instead.
      :param owner: Whether THIS wrapper releases the block at that address. It is
                    ignored when an address is not given - a report made here is the
                    wrapper's own and is released with it.

      :raises MemoryError: When a report of its own cannot be allocated.

   .. py:property:: address(self) -> int

      The block's address - what every read here reads through.

   .. py:property:: code(self) -> int

      The ``DCG_ERR_*`` code the pass ended with.

   .. py:property:: code_name(self) -> str

      That code's stable name.

   .. py:property:: node(self) -> ~decision_graph.decision_tree.bake.c_node.LogicNode | None

      The node the first problem was found on, or None when it was the pass's own.

   .. py:property:: errors(self) -> int

      How many problems the pass found.

   .. py:property:: nodes(self) -> int

      How many nodes the pass walked.

   .. py:property:: depth(self) -> int

      The deepest level the walk reached.

   .. py:property:: locked(self) -> int

      How many nodes this pass locked.

   .. py:property:: sealed(self) -> int

      How many stores this pass sealed.

   .. py:property:: capacity(self) -> int

      The entries the root's record was prepared with.
