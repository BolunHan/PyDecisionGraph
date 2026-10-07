Web UI
======

Overview
--------

One Flask-based viewer per API surface, each a sub-package of
``decision_graph.webui``:

- ``decision_graph.webui.capi`` — draws ``decision_graph.decision_tree.capi`` trees
- ``decision_graph.webui.native`` — draws ``decision_graph.decision_tree.native`` trees
- ``decision_graph.webui.bake`` — draws ``decision_graph.decision_tree.bake`` graphs

Each one names its own layer with a scoped import. None of them goes through
``decision_graph.decision_tree``, which re-exports whichever layer won at import
time; that is the point of the split, because a viewer is worth reading only if
it shows what ITS layer actually built.

Entry points
------------

The capi and native modules expose the same three doors:

- ``show(node, with_eval=True, **kwargs)`` — starts a local Flask server and
  opens the browser on an in-memory tree.
- ``watch(node, interval=0.5, block=False)`` — streams activation changes over
  SSE while the graph is re-evaluated on a timer. Needs a ``RootLogicNode``,
  whose evaluation record is what the stream diffs.
- ``to_html(node, file_name, with_eval=True)`` — writes a self-contained offline
  HTML file with the CSS, JS and D3 bundled into it.

The bake module's doors are described in its own section below, because a baked
graph is immutable and an un-baked one is not yet walkable.

How a tree reaches the page
---------------------------

The viewer converts a ``LogicNode`` tree into a JSON structure the front end
draws. A breakpoint node is rendered with a "virtual parent" link rather than a
child edge: the node it resumes into already sits under another parent, and
drawing it twice would double the subtree.

``DecisionTreeWebUi`` picks the next free port when the requested one is taken,
and tries to open the system browser.

Runtime requirements
--------------------

- Flask
- Jinja2 (used through Flask templates)

Install them if you plan to run a UI: ``pip install flask jinja2``.

Usage
-----

.. code-block:: python

    from decision_graph.webui.capi import DecisionTreeWebUi

    ui = DecisionTreeWebUi(host='127.0.0.1', port=5000, debug=False)
    ui.show(my_root_logic_node, with_eval=True)

Or, for a file you can open later with no server at all::

    from decision_graph.webui.native import to_html

    to_html(my_root_logic_node, 'tree.html')

API reference (autodoc)
-----------------------

.. automodule:: decision_graph.webui.capi.app
   :members:
   :undoc-members:
   :show-inheritance:

.. automodule:: decision_graph.webui.native.app
   :members:
   :undoc-members:
   :show-inheritance:
