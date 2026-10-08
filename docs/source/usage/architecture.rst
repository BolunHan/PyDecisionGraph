Architecture
============

This page is the map: where things live, what owns what, and which rules the
layers are held to. It is written to be acted on — every path and name here is
a path and name in the repository.

The repository
--------------

::

    decision_graph/
      __init__.py                  version (single source), logger, get_include()
      logic_group/                 SignalLogicGroup, PendingRequest, confirm flows
      decision_tree/
        exc.py                     the exception family (NodeError, EvalFailureError, ...)
        __init__.py                layer selection + the unified export surface
        capi/                      the compiled layer (Cython over the C core)
          c_abc.pyx/.pxd/.pyi        nodes, groups, the manager, actions
          c_node.pyx/.pxd/.pyi       RootLogicNode, context expressions
          c_collection.pyx/.pxd/.pyi LogicMapping, LogicSequence
        native/                    the pure-Python fallback, same surface
        bake/                      the C-first layer
          c_*.h                      the C core — header-only, one header per module
          c_*.pxd                    each header's own declarations (the owner)
          c_*.pyx                    thin Cython wrappers over the C core
          c_*.pyi                    the Python-facing stubs: signatures + docstrings
          __infra__.pxd              the hub: re-exports every pxd's C surface
          DEPENDENCY.md              the layer table and the sanctioned edge rules
      webui/
        capi/  native/  bake/      one Flask viewer per layer, each naming its own layer
    docs/examples/                 the runnable examples behind :doc:`examples`
    tests/                         the suites — see :doc:`../contributing/testing`
    probe.py                       macro probe: writes macros.json from the headers
    setup.py                       the extension build (17 extensions)
    build.sh / build.ps1           build wrappers (POSIX / Windows)
    nt_build.py / nt_steps.ps1     the Windows VM workflow

The three layers
----------------

The same decision model has three implementations, and the differences between
them are deliberate:

``decision_graph.decision_tree.capi``
    The compiled layer: Cython modules over the C core. This is what
    ``from decision_graph.decision_tree import ...`` resolves to when it is
    built (``USING_CAPI`` is then ``True``).

``decision_graph.decision_tree.native``
    A pure-Python fallback with the same *surface*, used when the compiled
    modules are absent. Its behaviour is documented where it differs — see
    :doc:`../decision_tree/fallback`.

``decision_graph.decision_tree.bake``
    The C-first layer: the C core under ``bake/*.h`` is the source of truth,
    the Cython wrappers are thin, and the docstrings live in the ``.pyi``
    stubs. It carries the two protocols below.

The C core
----------

The bake layer's C is **header-only**: every module is one ``c_<name>.h`` of
``static inline`` functions, consumed by Cython through a ``cdef extern``
mirror in the matching ``c_<name>.pxd``. The rules that keep this coherent are
worth knowing before editing anything in it:

- **Every header is owned by its own ``.pxd``** — that pxd declares the
  header's symbols completely. A module that needs symbols from a *higher*
  layer cannot ``cimport`` it (the cycle hands back an empty namespace); it
  declares what it needs locally with ``cdef extern from "<the header>"``.
- **The hub re-exports.** ``__infra__.pxd`` collects every pxd's public C
  surface; consumers cimport from the hub. A pxd change is not delivered until
  the hub has it.
- ``DEPENDENCY.md`` in the same directory is the design document: the layer
  table and the three sanctioned ways to reach upward (local extern block,
  ``cdef object`` + accessor, lazy import). Changes that ignore it are
  rejected in review.

The protocols
-------------

Two protocols drive the layer; both are C, in their own headers:

- **The evaluation protocol** (``c_eval.h``) — walks a graph. Each node carries
  the rule it is evaluated by; the protocol runs the node, records the stage
  and the producer into the node's own context, and appends the walk to the
  root's path record. Failures are reported as typed exceptions carrying the
  node, the stage and the code.
- **The bake protocol** (``c_bake.h``) — prepares a finished graph for the
  walk: **verify** (what the walk will assume), **lock** (freeze the
  structure), **prepare** (the record a walk fills). It runs once, from the
  root, and it is all-or-nothing. The same pass takes the scaffolding —
  breakpoints — down, splicing what they held into their place.

The allocator protocol
----------------------

All heap data in the C layer goes through an allocator from **PyCyBase**
(``cbase``): ``c_ap_alloc`` / ``c_ap_free`` with child blocks derived from
their parent. Two consequences matter at this level:

- Blocks can be placed in **shared memory**, which is what lets a graph be
  reconstructed by a *different process* from a bare address (the
  across-process tests — POSIX only, see :doc:`limitations`).
- The include path for the C core comes from ``cbase.get_include()``, so the
  PyCyBase installed in the environment is what the layer compiles against.

The build system
----------------

``setup.py`` builds **17 extensions**: the 15 package modules plus two
test-only probes. Three things it does are worth knowing:

- **``decision_graph/includes/`` is generated.** The build mirrors every
  ``.h``/``.c``/``.cpp`` under the package into it (and into the wheel) so
  downstream compilations can find the headers. Never edit that tree; edit the
  source headers.
- **``__init__.pxd`` is generated** from ``__infra__.pxd`` for the wheel; the
  build removes/restores it around cythonize. It is not a source file.
- **Compile-time macros are probed.** ``probe.py`` scans the pxds' extern
  headers for the ``#define`` macros and writes ``macros.json``; ``setup.py`` reads the
  inventory so any probed macro (and ``DEBUG``) can be overridden from the
  environment, e.g. ``DCG_VIGILANT=0 python setup.py build_ext --inplace``.

The exact commands — including the clean-rebuild recipe a header change
requires — are in :doc:`../contributing/development`.

The viewers
-----------

``decision_graph/webui/{capi,native,bake}`` holds one Flask viewer per layer.
Each viewer imports its own layer **by name** (``from decision_graph.decision_tree.capi import ...``)
rather than through ``decision_graph.decision_tree``, which re-exports whichever
layer won at import time — a viewer is worth reading only if it shows what its
layer actually built. All three layers expose the same three doors
(``show``, ``watch``, ``to_html``). What each viewer draws is in
:doc:`../webui`; snapshots are in :doc:`gallery`.
