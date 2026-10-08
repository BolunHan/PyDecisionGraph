c_node.RootLogicNode
======================

.. py:module:: decision_graph.decision_tree.capi.c_node
   :no-index:

.. py:class:: RootLogicNode(LogicNode)

      Entry point node for a decision graph.

      This node acts as the root container for a single child node and
      largely delegates evaluation and rendering to that child. It enforces
      that at most one child may be appended.

      Apart from a normal LogicNode, the RootLogicNode:
      - contains only 1 child node, with NO_CONDITION.
      - always returns True for `c_entry_check()`.
      - automatically shelve and unshelve contexts entering/exiting.
      - automatically triggers inspection mode on entering and restore to previous mode on exit.

      With these features, the RootLogicNode is designed as the Root node of a decision tree.

      :ivar inherit_contexts: Whether to inherit outer logic groups when entered.
      :ivar eval_path: List of nodes evaluated during the last evaluation. In cython interface this is a reflected copy, In python interface this is the actual list.

   .. py:attribute:: inherit_contexts

   .. py:attribute:: eval_path

   .. py:method:: __init__(self, name: str = 'Entry Point', inherit_contexts: bool = False, **kwargs) -> None

      Create a RootLogicNode.

      The constructor automatically passes the kwargs to underlying base classes. If any kwargs are provided, it can mess up the normal initializing process. It is recommended to not provide any kwargs and leave as is.

      .. rubric:: Example

      >>> with RootLogicNode() as root:
      ...     with LogicNode() as child:
      ...         ...
      ...     # There should not be a second node appended to root

      :param name: Optional name for the root node.
      :param \*\*kwargs: Implementation-specific options.

   .. py:method:: __enter__(self) -> ~decision_graph.decision_tree.capi.c_node.RootLogicNode

      Enter context manager for the root node, to build a new decision graph in a seperated context.

      On entering:
      0. The RootLogicNode itself will be appended to active node stack.
      1. Then the LGM will be shelved to preserve outer contexts.
      2. With ``inherit_contexts`` flags, the outer LogicGroup contexts will be inherited.
      3. After shelfing, a fresh LGM will be provided, with only this RootLogicNode activated.

      Then on exiting, the LGM will be restored to previous state.

      The __enter__ method is not overridden actually, only the internal c hook function.

      :returns: The RootLogicNode instance.

   .. py:method:: dry_run(self) -> None

      Perform a dry run evaluation of the decision tree without executing actions.

      This method traverses the decision tree starting from the root node,
      evaluating conditions and logging the evaluation path without
      executing any actions associated with the nodes.

      :raises ExpressEvaluationError: If an error occurs during evaluation.

   .. py:method:: get_breakpoint(self) -> ~decision_graph.decision_tree.capi.c_abc.BreakpointNode | None

      Get dangling breakpoint node attached to the root, if any.
      :returns: BreakpointNode if exists, else None.

   .. py:method:: append(self, child: ~decision_graph.decision_tree.capi.c_abc.LogicNode, condition: ~decision_graph.decision_tree.capi.c_abc.NodeEdgeCondition = NO_DEFAULT) -> None

      Append a child node to the root.

      :param child: The child logic node to append.
      :param condition: Edge condition associated with the child (ignored for
                        the root; defaults to None).

      :raises TooManyChildren: If a child is already attached to the root.

   .. py:method:: to_html(self, file_name: str | None = None, with_eval: bool = True) -> None

      Render the decision tree to an HTML file.

      This method generates a standalone HTML file visualizing the
      decision tree structure starting from this root node. If
      ``with_eval`` is True, the evaluation results are also included
      in the rendering.

      :param file_name: Output HTML file name; when omitted, the file is named
                        after the root's ``repr``.
      :param with_eval: Whether to include evaluation results in the rendering.

   .. py:method:: show(self, **kwargs)

      Render and display the decision tree in a interactive web page.
      This method generates an interactive visualization of the decision
      tree structure starting from this root node. Additional keyword
      arguments are passed to the underlying rendering flask engine.

      :param \*\*kwargs: keyword arguments passed into ``decision_graph.webui.capi.show`` method.

   .. py:method:: watch(self, **kwargs)

      Continuously monitor and update the decision tree visualization in a web page.

      This method sets up a live monitoring session where the decision
      tree structure and evaluation results are periodically refreshed
      and displayed in an interactive web page.

      :param \*\*kwargs: keyword arguments passed into ``decision_graph.webui.capi.watch`` method.

   .. py:property:: child(self) -> ~decision_graph.decision_tree.capi.c_abc.LogicNode

      Return the single child node attached to the root.

      :raises TooFewChildren: If no child is attached to the root.

