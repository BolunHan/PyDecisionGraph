LogicNode
====================

.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: LogicNode(LogicExpression)

      A decision node that branches to children based on an evaluated value.

      Each child is registered against an edge condition, and the node supports
      auto-inference of binary conditions for succinct graph construction.

      :ivar parent: The parent node, if any.
      :vartype parent: ~decision_graph.decision_tree.capi.c_abc.LogicNode | None
      :ivar condition_to_parent: The edge condition leading to this node from its parent.
      :vartype condition_to_parent: ~decision_graph.decision_tree.capi.c_abc.NodeEdgeCondition
      :ivar children: Mapping of edge conditions to child nodes.
      :vartype children: dict[~decision_graph.decision_tree.capi.c_abc.NodeEdgeCondition, ~decision_graph.decision_tree.capi.c_abc.LogicNode]
      :ivar labels: LogicGroup names this node belongs to.
      :vartype labels: list[str]
      :ivar autogen: Whether this node was auto-generated to fill a missing branch.
      :vartype autogen: bool

   .. py:attribute:: parent

   .. py:attribute:: condition_to_parent

   .. py:attribute:: children

   .. py:attribute:: labels

   .. py:attribute:: autogen

   .. py:method:: __init__(self, *, expression: object = None, dtype: type = None, repr: str = None, uid: uuid.UUID = None, **kwargs)

      Initialize the LogicExpression.

      :param expression: A callable or static value.
      :type expression: Union[Any, Callable[[], Any]]
      :param dtype: The expected type of the evaluated value (e.g. float, int, or bool).
      :type dtype: type, optional
      :param repr: A string representation of the expression.
      :type repr: str, optional
      :param uid: Unique identifier for the expression. If None, a new UUID is generated.
      :type uid: uuid.UUID, optional
      :param kwargs: __cinit__ extra kwargs guardian of for subclassing support, not used is this base class.

   .. py:method:: __rshift__(self, other: ~decision_graph.decision_tree.capi.c_abc.LogicNode) -> ~decision_graph.decision_tree.capi.c_abc.LogicNode

      Convenience for ``append`` to support chaining, e.g.::

      >>> node1 = LogicNode(expression=...)
      >>> node2 = LogicNode(expression=...)
      >>> node1 >> node2

      Returns the ``other`` node so calls can be chained.

   .. py:method:: __call__(self, default: Any = NO_DEFAULT) -> Any

      Evaluate the tree from this node and return the final action/value.

      If ``default`` is not provided, a ``NoAction`` node will be used as the
      fallback terminal.

      You can pass ``NO_DEFAULT`` to explicitly require a matching branch;
      if no edge matches, a ``ValueError`` will be raised.
      See ``eval_recursively`` for details.

   .. py:method:: append(self, child: ~decision_graph.decision_tree.capi.c_abc.LogicNode, condition: ~decision_graph.decision_tree.capi.c_abc.NodeEdgeCondition = AUTO_CONDITION) -> None

      Append a child node with the given edge condition.

      If ``condition`` is ``AUTO_CONDITION``, the condition is inferred
      automatically based on existing children (for binary branching).

      :raises ValueError: If an invalid condition is provided (e.g., ``None``).
      :raises KeyError: If the condition is already used by another child.
      :raises EdgeValueError: If condition inference fails due to incompatible existing children.
      :raises TooManyChildren: If inference fails due to too many existing children.

   .. py:method:: overwrite(self, new_node: ~decision_graph.decision_tree.capi.c_abc.LogicNode, condition: ~decision_graph.decision_tree.capi.c_abc.NodeEdgeCondition) -> None

      Overwrite the child node for the given edge condition.

      If ``condition`` is ``AUTO_CONDITION``, the condition is inferred
      automatically based on existing children (for binary branching).

      :raises ValueError: If an invalid condition is provided (e.g., ``None``).
      :raises KeyError: If there is no existing child for the given condition.
      :raises EdgeValueError: If condition inference fails due to incompatible existing children.
      :raises TooManyChildren: If inference fails due to too many existing children.

   .. py:method:: replace(self, original_node: ~decision_graph.decision_tree.capi.c_abc.LogicNode, new_node: ~decision_graph.decision_tree.capi.c_abc.LogicNode) -> None

      Replace an existing child node with a new node.

      :raises RuntimeError: If the original node is currently active.
      :raises LookupError: If the stack is out of sync with the node's children.
      :raises NodeNotFountError: If the original node is not a child of this node.

   .. py:method:: eval_recursively(self, path: list[~decision_graph.decision_tree.capi.c_abc.LogicNode] = None, default: Any = NO_DEFAULT) -> tuple[Any, list[~decision_graph.decision_tree.capi.c_abc.LogicNode]]

      Evaluate the decision tree recursively from this node.

      :param path: If provided, a list to record the
                   sequence of nodes traversed during evaluation.
      :type path: list[~decision_graph.decision_tree.capi.c_abc.LogicNode] | None
      :param default: The default value or action to use if no matching
                      child is found. Use ``NO_DEFAULT`` to request an error when no
                      branch matches.
      :type default: Any

      :returns:

                The resulting value/action and the
                    path list of nodes traversed during evaluation.
      :rtype: tuple[Any, list[LogicNode]]

   .. py:method:: list_labels(self) -> dict[str, list[~decision_graph.decision_tree.capi.c_abc.LogicNode]]

      List all LogicGroup names in the subtree rooted at this node.

      :returns:

                A mapping from label strings (logic group names)
                    to lists of nodes that have that label.
      :rtype: dict[str, list[LogicNode]]

   .. py:property:: leaves(self) -> Generator[~decision_graph.decision_tree.capi.c_abc.LogicNode]

      An iterable of all leaf nodes in the subtree rooted at this node.

   .. py:property:: is_leaf(self) -> bool

      True if this node has no children; otherwise False.

   .. py:property:: child_stack(self) -> Iterable[~decision_graph.decision_tree.capi.c_abc.LogicNode]

      An iterable of all child nodes in the subtree rooted at this node.

   .. py:property:: descendants(self) -> Generator[~decision_graph.decision_tree.capi.c_abc.LogicNode, None, None]

      A generator yielding all descendant nodes in the subtree rooted at this node.
