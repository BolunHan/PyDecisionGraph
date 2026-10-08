NodeEvalPathView
================

.. py:module:: decision_graph.decision_tree.bake.c_hierarchy
   :no-index:

.. py:class:: NodeEvalPathView

      A read-only window onto ``dcg_node_eval_path``: the record of a walk.

      A view is an ADDRESS, not a copy: it holds the record it was taken over, and
      every read answers for what that record holds now. The record belongs to the
      root whose walk wrote it - it is embedded in that root and its blocks are
      nested under it - so the view must not outlive the root, which the layer
      guarantees by handing one out only from the root it belongs to
      (``RootLogicNode.eval_path``).

      It cannot write: the view holds the record **const**, and every reader it
      exposes reads through that pointer, so a write through a view does not
      compile.

      :ivar _header: The C record this view reads through.

   .. py:method:: __init__(self, address: int) -> None

      View the record at an address.

      :param address: Address of a ``dcg_node_eval_path``. The layer hands these
                      out itself (``RootLogicNode.eval_path``); nothing here checks that
                      the address is a record, and the view keeps nothing alive.

   .. py:method:: __repr__(self) -> str

      The view's own text: the count, the outcome and where the walk landed.

   .. py:method:: __len__(self) -> int

      How many nodes the walk reached.

   .. py:method:: __iter__(self) -> Iterator[~decision_graph.decision_tree.bake.c_node.LogicNode]

      Walk the entries: the nodes, in the order the walk reached them.

   .. py:method:: __getitem__(self, index: int) -> ~decision_graph.decision_tree.bake.c_node.LogicNode

      The entry at an index, counting from the front or the back.

      :param index: Which entry; negative counts from the end.

      :returns: The wrapper for the node the walk reached there.

      :raises IndexError: When there is no such entry.

   .. py:property:: address(self) -> int

      The record's address - what this view reads through.

   .. py:property:: nodes(self) -> list[~decision_graph.decision_tree.bake.c_node.LogicNode]

      Every entry, as a list: the nodes in the order the walk reached them.

   .. py:property:: capacity(self) -> int

      The room the record has - how many entries it can hold before it grows.

   .. py:property:: code(self) -> int

      The outcome, as the ``DCG_ERR_*`` code the walk ended with.

   .. py:property:: code_name(self) -> str

      The outcome's stable name: ``'OK'``, ``'UNBOUND'``, ``'NO_MATCH'``, ...

   .. py:property:: leaf(self) -> ~decision_graph.decision_tree.bake.c_node.LogicNode | None

      The node the walk came to rest on, or None when it never landed.

   .. py:property:: failed(self) -> ~decision_graph.decision_tree.bake.c_node.LogicNode | None

      The node that reported the failure, or None when nothing failed.

      In a walk this is the node whose own evaluation refused - the one whose
      stages say how far it got - which need not be the node a caller asked to
      evaluate: a root that reached a branch which then failed did not fail.

   .. py:property:: seq_id(self) -> int

      The id of the run the record belongs to: which walk wrote it.
