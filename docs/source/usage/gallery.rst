Gallery
=======

Snapshots of what the three viewers and the bake layer's exports actually draw.
Every image here was captured from a page this release produced — the sources
are listed with each one, and the section at the end says how to regenerate
them.

The capi viewer
---------------

The compiled layer's viewer, drawing the README's tree: a tree layout, each
edge chip labelled with its condition (``True`` / ``False`` / ``Unconditional``),
logic-group regions drawn as boxes, and the active path highlighted.

.. image:: /_static/gallery/capi-viewer.png
   :alt: The capi viewer drawing the quick-start decision tree
   :width: 100%

Source: ``root.to_html()`` from the quick-start script (page written as
``Entry Point.html``, after the root's default name).

The native viewer
-----------------

The pure-Python fallback draws the same way — one viewer per layer, and each
draws what *its* layer built:

.. image:: /_static/gallery/native-viewer.png
   :alt: The native viewer drawing the same tree through the fallback layer
   :width: 100%

The bake viewer — as the build left it
--------------------------------------

The bake layer's viewer draws cards on a grid. This is the deep tree **before**
the bake: the title bar counts the scaffolding the graph is still carrying —
breakpoints standing where a build stopped — and every card names its type,
its display text, its store labels and its subtree size.

.. image:: /_static/gallery/bake-unbaked.png
   :alt: The bake viewer drawing the deep tree before the bake, scaffolding counted
   :width: 100%

The bake viewer — baked
-----------------------

The same graph after ``bake()``: the scaffolding count is zero, the stand-ins
have been consolidated away, and the graph is locked.

.. image:: /_static/gallery/bake-baked.png
   :alt: The same tree after baking, no scaffolding left
   :width: 100%

The bake viewer — walked
------------------------

After an evaluation, the walk is drawn: the path from the root to the leaf it
came to rest on is highlighted, the sidebar reports the outcome (code, walk
length, the leaf), and the store values the walk read are on the cards.

.. image:: /_static/gallery/bake-walked.png
   :alt: The baked tree after a walk, the active path highlighted
   :width: 100%

The split build, baked
----------------------

A graph assembled by two functions through a breakpoint, baked: the breakpoint
is gone and the continuation it held stands in its place. Compare the walked
path — it crosses where the break used to be as if nothing had ever stood
there.

.. image:: /_static/gallery/bake-split.png
   :alt: The split-built graph after baking, the breakpoint spliced away
   :width: 100%

Regenerating the gallery
------------------------

The pages come from the artifact generator and the examples; the screenshots
come from a headless browser:

.. code-block:: bash

    # 1. the bake pages (unbaked / baked / walked / split), written to tests/artifacts/bake_webui/
    python tests/bake_webui_artifacts.py

    # 2. the capi and native pages
    python docs/examples/quickstart.py          # writes tree.html (rename it Entry Point.html)
    python docs/examples/native_export.py

    # 3. screenshot one: a dedicated profile keeps a running browser out of the way
    firefox --headless --profile /tmp/ff-gallery \
        --screenshot docs/source/_static/gallery/bake-walked.png \
        --window-size=1920,1080 "file://$PWD/tests/artifacts/bake_webui/03_deep_tree_baked_walked.html"
