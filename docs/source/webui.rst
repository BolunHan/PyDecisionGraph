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

The bake module exposes ``show``, ``watch`` and ``to_html`` with the same
signatures, and a ``RootLogicNode`` of that layer carries ``show()``,
``watch()`` and ``to_html()`` of its own. Only the drawing differs.

How the bake viewer draws
-------------------------

A node is a card, and the cards are laid out on a grid: the tree layout supplies
each node's place in its sibling order and its depth, and those two indices are
multiplied by a fixed cell size, so the same graph lands in the same place every
time and an export is the picture on screen rather than a second drawing of it.

- **Orientation** — which axis depth runs along. Top-to-bottom puts the breadth
  on x, left-to-right puts it on y; the arm order and the condition chips are
  the same either way.
- **Theme** — dark and light are one attribute on ``<html>``, and everything the
  drawing uses is a custom property, so an export can snapshot the values it
  finds and the SVG is styled by the same names the page is.
- **The card** carries the type, the display text, the store labels, the value
  the node holds itself (a literal's value, a read's entry), the value it last
  produced, and the size of its subtree. Its band is coloured by type where the
  type says more than the family does: an action is green to go long, red to go
  short, yellow to cancel, grey to do nothing.
- **Operands** are not children, and are listed as their own thing: an
  expression reads nodes the tree does not reach, so the inspector names them
  and the card counts them.
- **Presentation** — the level gap, the sibling gap, the card's width and its
  height, and the edge width are sliders, because how much of a graph fits on a
  screen is the reader's call rather than the layout's. The width is what a
  display text wraps to and the height is how much of it fits: a long
  expression needs one or the other raised, and a card scaled by a transform
  would only make the same overflow bigger.
- **The sidebar folds.** Every panel keeps its title and puts its body away,
  and the presentation sliders start folded at the bottom — they are the
  controls a reader reaches for once, not the ones they keep open.
- **Collapsing a node animates.** The drawing is rebuilt rather than moved, so
  a card that has changed place is put back where it was and allowed to travel,
  and one that has left keeps a copy on screen to fade away. The frames are
  written by the page rather than handed to CSS: a transition needs the browser
  to have committed a value to travel FROM, and these nodes were made a moment
  earlier in the same turn — so whether it sees two states or one coalesced
  write is the browser's choice, and browsers differ. The animation slider
  scales the duration, from ×2 down to ×0.01, which is slow enough to watch a
  single card travel.
- **Scaffolding** — a breakpoint a split build stopped at, and a stand-in a
  branch reserved, are what an un-baked graph still carries and a baked one does
  not. The title bar counts them, because that count is what tells the two
  states of a graph apart.
- **Clipboard** — the walked path is a list of node ids, and it crosses between
  a session and the page in that form: *Copy path* writes it, the box takes one
  pasted in, and *Load* highlights the ids it names.

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

.. automodule:: decision_graph.webui.bake.app
   :members:
   :undoc-members:
   :show-inheritance:
