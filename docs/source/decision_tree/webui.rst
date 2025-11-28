Decision Tree Web UI
=====================

Overview
--------

The Decision Tree Web UI is a small Flask-based viewer that renders
LogicNode trees as an interactive D3 visualization. It's implemented in
`decision_graph.decision_tree.webui.app` and provides two main entry
points:

- `DecisionTreeWebUi.show(node, with_eval=True)` — starts a local Flask
  server and opens the browser to visualize an in-memory `LogicNode` tree.
- `DecisionTreeWebUi.to_html(node, file_name, with_eval=True)` — renders a
  self-contained offline HTML file (bundles CSS/JS into the output file).

Design summary
--------------

- The viewer converts a `LogicNode` tree into a JSON structure suitable
  for D3. Breakpoint nodes are represented with "virtual parent" links
  to preserve logical connections that don't follow direct child edges.
- A small set of routes are exposed:
  - `/` — the main viewer page (loads tree data via JS)
  - `/api/tree_data` — JSON endpoint that returns the tree data for the
    currently-loaded tree
- `DecisionTreeWebUi` will try to pick an available port if the requested
  port is in use; it also attempts to open the system browser automatically.

Runtime requirements
--------------------

The web UI requires these Python packages at runtime:

- Flask
- Jinja2 (used via Flask templates)

If you plan to run the UI, install them into your environment (for
example: `pip install flask jinja2`).

Usage example
-------------

The common usage is programmatic — build or obtain a `LogicNode` tree in
your application and call `DecisionTreeWebUi.show()` on it, e.g.:

.. code-block:: python

    from decision_graph.decision_tree.webui.app import DecisionTreeWebUi

    ui = DecisionTreeWebUi(host='127.0.0.1', port=5000, debug=False)
    ui.show(my_root_logic_node, with_eval=True)

API reference (autodoc)
-----------------------

.. automodule:: decision_graph.decision_tree.webui.app
   :members:
   :undoc-members:
   :show-inheritance:

