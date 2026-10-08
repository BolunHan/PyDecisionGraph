Limitations and notes
=====================

What the library does not do, where the layers differ, and the sharp edges
worth knowing before building on them. Everything here is current behaviour of
this release, not aspiration.

Building
--------

- **Store and group names are registered process-wide.** Two stores
  (``LogicMapping``), or two groups (``LogicGroup``), cannot share a name in one process — a second one
  is refused with a duplicate error. Names must be unique per process, not per
  tree; a helper that appends a counter is the usual answer.
- **The root takes a single child.** Everything else nests under that one
  block; a second top-level block under the root raises ``TooManyChildren``.
- **An arm can hold one node.** A node has exactly two arms (true and false),
  so a ``with`` block may contain at most the two child blocks that fill them.
- **A read binds to the store that is active when it is created.** Building a
  read inside a group that holds no entries gives it no store; enter the store
  (``with book:``) before reading.
- **An empty arm is auto-filled.** When a walk reaches an arm nothing filled,
  it is answered by a no-action stand-in rather than failing — the walk lands
  on a ``NoAction``.

The two comparable layers
-------------------------

- ``capi`` and ``native`` share a surface but not, in every corner, semantics:
  for example, ``==`` on an expression returns a *logic expression* in capi and
  a plain ``bool`` in native. The differences are listed on
  :doc:`../decision_tree/fallback`; the test suite pins each layer's own
  behaviour rather than pretending they are identical.
- **A breakpoint is scaffolding only in the bake layer.** Both layers let a
  build stop at a breakpoint and be carried on from it — ``break_(…)`` where
  the branch should stop, then ``root.get_breakpoint()`` and ``with
  breakpoint:`` in the function that carries on — but the walk sees different
  graphs: capi keeps the breakpoint and walks through it, while ``bake()``
  takes it down and the continuation takes its place.
- **A capi breakpoint holds exactly ONE continuation.** Entering it and
  building two sibling ``with`` blocks links the second and leaves the first
  unlinked; write the continuation as one block, or use the bake layer.

The bake layer
--------------

- **Baked means frozen.** ``bake()`` locks the structure: after it, new nodes
  are refused. Values still move through the store — the shape does not.
- **The bake is all-or-nothing.** A failed bake changes nothing and carries
  the failure in its report (``BakeFailureError``).
- **Scaffolding does not survive.** Breakpoints are taken down by the bake; a
  page drawn after the bake shows the continuation in the break's place.
- **A root with a parent cannot be baked** (a sub-root composition — one root
  built inside another root's scope — is refused by ``bake()``); assembling
  split graphs with breakpoints is the supported route.
- **The viewer lists operands, it does not draw them.** An expression's
  operands are shown in the inspector as data, not as edges, because they are
  not children of the tree.

Platforms
---------

- **Python 3.12+**; wheels are built for Linux and Windows (CPython 3.12,
  3.13, 3.14).
- **The across-process reconstruction is POSIX-only.** Its child is a fork —
  the shared allocator region is unlinked on map, so only a forked process can
  see the graph's addresses. Those three tests skip on Windows, by design.
- **The C test matrix is POSIX-only** (GNU make + GCC/Clang, six
  configurations). Windows runs the Python suite, compiled with MSVC, through
  the ``nt_build.py`` workflow — see :doc:`../contributing/development`.
- **C sources are ASCII by rule.** MSVC compiles under the system code page;
  non-ASCII literals in a header or ``.pyx`` break the Windows build or corrupt
  the emitted bytes. (The tree's one non-ASCII feature — the box-drawing tree
  renderer — spells its glyphs as UTF-8 byte escapes for exactly this reason.)
