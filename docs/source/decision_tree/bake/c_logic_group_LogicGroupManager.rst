LogicGroupManager
=================

.. py:module:: decision_graph.decision_tree.bake.c_logic_group
   :no-index:

.. py:class:: LogicGroupManager

      The state of a build: the stacks of groups, nodes and breakpoints.

      It is the caller's struct, not an allocated block, and it keeps the
      allocator it was initialised with. A node entered with the `with` statement
      is placed by this manager - into the active node's reserved arm - and the
      manager knows the group a node belongs to.

   .. py:method:: __init__(self) -> None

      Initialize an empty manager: no groups, no nodes, no breakpoints.

   .. py:method:: register(self, group: ~decision_graph.decision_tree.bake.c_logic_group.LogicGroup) -> None

      Register a group under its name and its address.

      :param group: Group to register - by name when it has one, always by
                    address.

   .. py:method:: find(self, name: str) -> ~decision_graph.decision_tree.bake.c_logic_group.LogicGroup | None

      The group registered under a name.

      :param name: The group's name.

      :returns: The group, or None when no group is registered under that name.

   .. py:method:: shelve(self) -> None

      Put the current build away, to be taken back by ``unshelve``.

   .. py:method:: unshelve(self) -> None

      Take back a build that was shelved.

   .. py:method:: clear(self) -> None

      Empty the manager: every stack, shelved state included.

   .. py:method:: label_node(self, node: ~decision_graph.decision_tree.bake.c_node.LogicNode) -> None

      Label a node with the names of the groups it was built inside.

      :param node: Node to label.

   .. py:method:: node_stack_append(self, node: ~decision_graph.decision_tree.bake.c_node.LogicNode) -> None

      Push a node onto the manager's node stack.

      :param node: Node being entered.

   .. py:method:: node_stack_pop(self, node: ~decision_graph.decision_tree.bake.c_node.LogicNode) -> None

      Pop a node off the manager's node stack.

      :param node: Node being left.

   .. py:property:: registry(self) -> LogicGroupNameRegistry

      The manager's own name index.

   .. py:property:: inspection_mode(self) -> bool

      Whether a build records the path an evaluation takes.

   .. py:property:: vigilant_mode(self) -> bool

      Whether the layer reports a mistake where it is made.

   .. py:property:: active_group(self) -> ~decision_graph.decision_tree.bake.c_logic_group.LogicGroup | None

      The group a build is currently inside, or None outside any group.

   .. py:property:: active_node(self) -> ~decision_graph.decision_tree.bake.c_node.LogicNode | None

      The node a build is currently inside, or None outside any node.

   .. py:property:: address(self) -> int

      The manager's own address, which is how the layer names it to C.

   .. py:property:: n_groups(self) -> int

      How many groups are entered.

   .. py:property:: n_nodes(self) -> int

      How many nodes are entered.

   .. py:property:: n_breakpoints(self) -> int

      How many breakpoints are queued and waiting.
