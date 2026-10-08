Concepts
========

Five ideas carry the whole library. Read this once and the rest of the docs —
and the API reference — will read as variations on them.

Nodes and arms
--------------

A decision graph is made of **nodes**. A node evaluates its condition and walks
into one of its **two arms**:

- the **true arm** — where the walk goes when the condition holds;
- the **false arm** — where it goes when it does not.

In the builder syntax, the arms are the nested ``with`` blocks: the **first**
block under a node fills its true arm, the **second** fills its false arm.
A node with one child has one arm filled and the other empty; an empty arm is
answered with a no-action stand-in when the tree is evaluated.

**Actions** (``LongAction``, ``ShortAction``, ``NoAction``, ``CancelAction``,
``ClearAction``) are the leaves — the outcomes a walk lands on.

.. code-block:: python

    with book.volatility > 0.25:      # the node
        ShortAction()                 # true arm
        LongAction()                  # false arm

The root
--------

``RootLogicNode`` is the entry point. It takes a **single child** — the top of
the tree — and refuses a second. You enter it once (with ``with``); everything
you build inside hangs from that one child. This is why the README's tree has
one top-level block and everything else nested inside it.

Stores, reads and groups
------------------------

A ``LogicMapping`` is a **store**: named values that conditions read from.
Reading an attribute (``book.volatility``) builds a **read node** — the read is
part of the graph, not a Python value, so the walk evaluates it where it
stands.

- Reads answer **live**: feeding the store a new value moves the next walk.
  Nothing is cached at build time.
- A read binds to the store the build is **running inside** — the store whose
  ``with`` block is active when the read is created. Nest two stores' blocks
  and the nodes built inside carry both labels.
- Store and group **names are registered process-wide**, so two stores or
  groups cannot share a name in one process.

Expressions
-----------

Comparisons (``>``, ``<``, ``==``, ``!=`` …), combinations (``&``, ``|``) and
arithmetic (``+``, ``-``, ``*`` …) on reads build **expression nodes**. An
expression's operands are nodes, but not children — they are read by the
expression, not walked into by the tree. See :doc:`../usage/examples` for
expressions in action.

Groups and breakpoints
----------------------

A ``LogicGroup`` scopes a build: it names a region of the tree and can be left
early with ``break_``. Where a branch stops, a **breakpoint** stands — and a
*second* function can enter that breakpoint and carry the branch on. Breakpoints
are *scaffolding*: they are how several functions (or several stores' worth of
logic) assemble one graph.

Baking
------

The **bake layer** (``decision_graph.decision_tree.bake``) is the layer built
for running a finished graph in a loop. ``root.bake()`` — called once — verifies
the graph, locks it against structural change, takes the scaffolding down
(the breakpoints), and prepares the record every walk fills. After a bake the
graph is **frozen**: values still move through the store, but the shape does
not. A bake is **all-or-nothing**: it either prepares the whole graph or
changes nothing.

The three layers
----------------

The same decision model exists in three implementations:

- ``decision_graph.decision_tree.capi`` — the compiled layer (Cython over C),
  the default when it is built;
- ``decision_graph.decision_tree.native`` — a pure-Python fallback with the
  same surface, for environments without the compiled modules;
- ``decision_graph.decision_tree.bake`` — the C-first layer with the eval and
  bake protocols, for hot-loop use.

``from decision_graph.decision_tree import ...`` picks capi when available and
native otherwise (``USING_CAPI`` says which). The bake layer is always imported
explicitly. :doc:`../usage/architecture` draws the whole picture.
