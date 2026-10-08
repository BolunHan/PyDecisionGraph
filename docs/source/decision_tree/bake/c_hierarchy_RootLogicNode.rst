RootLogicNode
=============

.. py:module:: decision_graph.decision_tree.bake.c_hierarchy
   :no-index:

.. py:class:: RootLogicNode(LogicNode)

      The graph's entry point: the only node that has no parent.

      A root takes exactly ONE branch - its single arm is the entry edge - and
      entering it shelves the contexts around it, so the graph a build enters is
      built in a context of its own.

      It is also where evaluating a graph starts: ``eval`` walks from here to a
      single leaf and keeps the record of how it got there, so the root is what a
      caller asks to make a decision.

      :ivar name: The root's name, as the build gave it.

   .. py:method:: __init__(self, *, name: str = 'Entry Point', inherit_contexts: bool = False, **kwargs: Any) -> None

      Build a root.

      :param name: The root's name.
      :param inherit_contexts: Whether it takes the contexts of the groups around
                               it rather than shelving them.

      :raises MemoryError: When the block cannot be allocated.

   .. py:method:: eval(self) -> Any

      Evaluate the graph from this root, and record how it decided.

      The whole walk, not one node - which is what a root does differently
      from every other node, whose ``eval`` runs its own rule and nothing else.
      Every node on the way is evaluated, the record of the last walk is
      replaced, and the answer is where the walk came to rest: the leaf itself
      when that leaf is an action, because the action IS the decision, and the
      value in the leaf's slot otherwise.

      The value a walk reads is the value AT THE MOMENT OF THE WALK: a read is
      live, so the same graph decides differently when the store it reads
      changes, and evaluating again is how a caller asks it to. What a walk
      pins is the TYPE: a read is resolved to its entry's type at its first
      evaluation, and a value of another type written to that entry afterwards
      is not one it reports.

      :returns: The leaf's wrapper when the walk landed on an action, else the value
                that leaf holds.

      :raises EvalFailureError: When the walk could not reach a leaf - a read of an
          entry with no value in it, a branch whose value selected no arm
          and which has no fallback, or a node a hook refused. It names the
          node that refused, with the code, the stage and what was running
          (``node`` is the node the record reports as ``failed``), and a
          hook's own exception travels as its cause. The record still holds
          the nodes reached before it stopped.

   .. py:method:: __call__(self) -> Any

      Evaluate the graph - the same walk ``eval`` runs, written as a call.

      A root is what a caller asks to make a decision, and asking it is the
      call: ``root()`` and ``root.eval()`` answer with the same value for the
      same graph and leave the same record. The call form is what a graph reads
      as at the place a decision is wanted, and it is the one door the two
      layers spell the same way.

      :returns: The leaf's wrapper when the walk landed on an action, else the value
                that leaf holds.

      :raises EvalFailureError: When the walk could not reach a leaf - the same
          failures, reported the same way, as ``eval``.

   .. py:method:: bake(self, validate_only: bool = False) -> BakeReport

      Bake the graph: verify what an evaluation assumes, lock it, prepare it.

      The last thing done to a graph before it is walked, and the pass every
      evaluation is written assuming has run. What an evaluation assumes is
      established here and nowhere else, because the hot path carries no check
      for a malformed node:

      - the graph's STRUCTURE - one parentless root, the arms in reading order,
        an else last, an action below nothing, no cycles;
      - every operand an operator takes has a component bound to it, and its
        operand array is exactly as long as its arity - the rules run their
        operands and read the workspace they filled with nothing checked in
        between;
      - every operator belongs to its node's arity, and no node in the graph is
        one that has no evaluation at all (a call).

      What the pass then does is hold the graph to what it verified: every node
      it walked is frozen, so the structure is what it was, and every store the
      graph reads is sealed, so no entry can appear in it afterwards. That
      second half is the one an evaluation depends on - a resolved read holds a
      reference into its store's block, and a block moves only when the store
      grows. A store is sealed in SHAPE and not in contents: the entries it has
      are the entries it will have, and a caller goes on writing values into
      them, because a graph is baked once and fed many times.

      Finally the record a walk fills has its room made in advance, sized for
      what a walk from this root can need, so evaluating a baked graph
      allocates nothing.

      A bake is all or nothing: a graph that fails is not locked and not
      prepared, so nothing about it changes. A bake that succeeds is
      idempotent - asked again it locks nothing and seals nothing, which the
      report says.

      :param validate_only: Whether to verify the graph and report without
                            locking or preparing it - the question "would this bake?" asked
                            without the commitment. Nothing about the graph changes.

      :returns: what the pass found, and what it affected.
      :rtype: The report

      :raises BakeFailureError: When the graph cannot be baked - the code, the node
          that produced it and the problems found are in the report the
          failure carries, and nothing was locked or prepared.

   .. py:method:: get_breakpoint(self) -> ~decision_graph.decision_tree.bake.c_hierarchy.BreakpointNode | None

      The breakpoint this graph left waiting, or None if it left none.

      A breakpoint that has not resumed into anything yet IS a leaf - it is
      where a branch stopped - so what this looks for is the first breakpoint
      among the leaves: the one a build can enter to carry the graph on from
      where it broke out.

      That is what makes a graph assemblable from several places. One function
      builds up to a break, this hands the break back, and the next function
      takes it and builds the rest - so the two halves never have to be in the
      same scope, or even the same function.

      :returns: The waiting breakpoint, or None when the graph has none.

   .. py:method:: to_html(self, file_name: str | None = None, with_eval: bool = True) -> None

      Render this graph to a standalone HTML file.

      The file carries the stylesheet, the script and D3 inside it, so it
      opens anywhere with no server and no network.

      :param file_name: Output HTML file name; when omitted, the file is named
                        after the root's ``repr``.
      :param with_eval: Whether to include the last evaluation in the rendering.

   .. py:method:: show(self, **kwargs: Any) -> None

      Serve this graph in the layer's own web viewer and open a browser.

      :param \*\*kwargs: keyword arguments passed into ``decision_graph.webui.bake.show`` method.

   .. py:method:: watch(self, **kwargs: Any) -> None

      Stream this graph's changes to the layer's own web viewer.

      The root is re-evaluated on a timer, and what changed between runs is
      sent to the page as it happens.

      :param \*\*kwargs: keyword arguments passed into ``decision_graph.webui.bake.watch`` method.

   .. py:property:: inherit_contexts(self) -> bool

      Whether this root takes the groups around it rather than shelving them.

      Recorded on the root when it is built, and read back off the block. Note
      that this layer's root does not shelve yet - its entering reserves its arm
      and nothing else - so the flag is carried and reported but has no effect
      on a build. The capi's root does shelve, which is where the two differ.

   .. py:property:: name(self) -> str | None

      The root's name - None for a root rebuilt from C, which carries none.

   .. py:property:: eval_path(self) -> NodeEvalPathView

      The record of how the last decision was made, as a view over it.

      The root first and the leaf it came to rest on last, with every branch it
      descended through in between - the very wrappers the build made, not
      fresh views of them - plus the outcome: which code the walk ended with,
      where it landed, what failed if anything did, and which run wrote it. It
      is replaced by each walk, so what it holds is the last one; a walk that
      could not finish leaves the nodes it got through and no leaf.

      The view is taken once, when the root is built, and it reads the record
      the root holds - so a root rebuilt from C carries one too, over the very
      record the block has. It is a field rather than a computed value: asking
      for it twice answers with the same view.
