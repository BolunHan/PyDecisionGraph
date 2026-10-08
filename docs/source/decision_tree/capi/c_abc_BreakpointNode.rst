c_abc.BreakpointNode
======================

.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: BreakpointNode(LogicNode)

      A logic node that represents a breakpoint in the decision tree, used for breaking out of logic groups.

      This node is auto-generated and can connect to at most one child node.
      To connect as branch to a BreakpointNode, there are 2 ways:
      - A managed style, powered by LGM, where the BreakpointNode connects to the next entered node, outside the LogicGroup this BreakpointNode from. after a break is recorded.
      - or a manual way, using with clause. Which entered the BreakpointNode context, and then continue to build the rest of the branch. In this way the LGM will relinquish the management of this node. This could be potentially dangerous, but provides a way to join external branches.

      During evaluation, if connected, it delegates to the child's evaluation; otherwise, it returns its default expression (NoAction) in vigilant mode.

      :ivar break_from: The logic group from which this breakpoint breaks.
      :vartype break_from: ~decision_graph.decision_tree.capi.c_abc.LogicGroup
      :ivar await_connection: Whether to wait for a connection to a child node during inspection.
      :vartype await_connection: bool

   .. py:attribute:: break_from

   .. py:attribute:: await_connection

   .. py:method:: __init__(self, *, break_from: ~decision_graph.decision_tree.capi.c_abc.LogicGroup = None, expression: Any = None, repr: str = None, **kwargs)

      Initialize the BreakpointNode.

      :param break_from: The logic group from which this breakpoint breaks. Defaults to None.
      :type break_from: ~decision_graph.decision_tree.capi.c_abc.LogicGroup
      :param expression: The default expression for this breakpoint node. Defaults to a new unique dangling NoAction node.
      :type expression: Any
      :param str: A string representation of the expression. Auto-generated if None.
      :type str: str

   .. py:method:: break_(cls, break_from: ~decision_graph.decision_tree.capi.c_abc.LogicGroup, **kwargs) -> ~decision_graph.decision_tree.capi.c_abc.BreakpointNode
      :classmethod:

      Create a BreakpointNode that breaks from the given logic group, for full manual control.
      This method also auto connects the BreakpointNode to the active node in the LGM upon creation.

      :param break_from: The logic group from which this breakpoint breaks.
      :type break_from: ~decision_graph.decision_tree.capi.c_abc.LogicGroup
      :param kwargs: Additional keyword arguments to pass to the BreakpointNode constructor.

   .. py:method:: connect(self, child: ~decision_graph.decision_tree.capi.c_abc.LogicNode) -> None

      Connect a child node to this BreakpointNode.

      :raises TooManyChildren: If a child is already connected.

   .. py:property:: linked_to(self) -> ~decision_graph.decision_tree.capi.c_abc.LogicNode

      The child node connected to this BreakpointNode.

      :returns: The connected child node, if there is one, otherwise return None.
      :rtype: LogicNode

