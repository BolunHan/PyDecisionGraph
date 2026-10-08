BreakpointNode
==============

.. py:module:: decision_graph.decision_tree.bake.c_hierarchy
   :no-index:

.. py:class:: BreakpointNode(LogicNode)

      An inspection sink: evaluation stops here and resumes outside the group.

      It borrows the group it breaks out of, and this wrapper is what holds that
      group alive while the breakpoint stands - which is why a reconstruction
      restores the group with it.

   .. py:method:: __init__(self, *, break_from: ~decision_graph.decision_tree.bake.c_logic_group.LogicGroup | None = None, repr: str | None = None, autogen: bool = True, **kwargs: Any) -> None

      Build a breakpoint.

      :param break_from: The group it breaks out of, if any.
      :param repr: Display text to copy.
      :param autogen: Whether the builder generated it rather than a caller.

      :raises MemoryError: When the block cannot be allocated.

   .. py:method:: break_(cls, break_from: ~decision_graph.decision_tree.bake.c_logic_group.LogicGroup, **kwargs: Any) -> ~decision_graph.decision_tree.bake.c_hierarchy.BreakpointNode
      :classmethod:

      Break out of a group, and hand back the breakpoint that did it.

      A break is a node: it takes the arm the build was about to fill, and the
      build continues from IT rather than from the group it left. That is what
      makes this the door for assembling a graph in pieces - one function
      builds up to the break and returns, and the break it made is taken here
      to carry on outside the group.

      The breakpoint is built first and then installed, rather than made by the
      install: the block that lands in the graph has to be the one this wrapper
      holds, or the caller is handed a handle on a node that is not in the
      graph.

      :param break_from: The group being broken out of.
      :param \*\*kwargs: Passed to the constructor.

      :returns: The breakpoint, now in the graph and waiting to resume into the next
                node built.

      :raises RuntimeError: When the C layer cannot place it.
      :raises MemoryError: When the block cannot be allocated.

   .. py:method:: connect(self, child: ~decision_graph.decision_tree.bake.c_node.LogicNode) -> None

      Connect a node as what this breakpoint resumes into.

      A breakpoint resumes into exactly one node - which is what its arm is
      for - so a second connection is refused rather than replacing the first.

      :param child: Node to connect.

      :raises RuntimeError: When the C layer refuses the link.

   .. py:property:: linked_to(self) -> ~decision_graph.decision_tree.bake.c_node.LogicNode | None

      The node this breakpoint resumes into, or None while it waits.

   .. py:property:: break_from(self) -> ~decision_graph.decision_tree.bake.c_logic_group.LogicGroup | None

      The group this breakpoint breaks out of, or None when it names none.

   .. py:property:: await_connection(self) -> bool

      Whether the breakpoint is waiting for the node it resumes into.
