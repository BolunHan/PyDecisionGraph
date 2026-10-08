Testing
=======

The suites, what each one covers, and the exact commands to run them. Nothing
here is optional ceremony: the C matrix catches what the Python suite cannot
(a rename's leftovers in headers), and the parity harness is what holds the
three protocol implementations to one behaviour.

The Python suite
----------------

.. code-block:: bash

    python -m pytest tests/ -q

- ``tests/test_bake_*.py`` — the bake layer's wrapper surface, one file per
  module family (nodes, consts, exprs, actions, edges, collections, hierarchy,
  logic groups, evaluate, reconstruct, var, imports, tree build).
- ``tests/bake/eval_protocol/`` — the evaluation protocol, twelve indexed
  files (single nodes, composed nodes, store-driven values, with-clause
  graphs, mid/deep graph walks, dry runs, eval paths, protocol edges, scalar
  dunders, failures).
- ``tests/bake/bake_protocol/`` — the bake protocol, eight indexed files (the
  door, the lockdown, the store, the record, the walk, what is refused,
  validate-only, a baked graph deciding).
- ``tests/test_capi_*.py`` / ``tests/test_native_*.py`` — the two comparable
  layers, each pinned to *its own* behaviour where they differ.
- ``tests/test_webui*.py`` — the viewers: the layer split, the bake viewer's
  routes and drawing model.
- ``tests/test_docs_examples.py`` — runs every script under
  ``docs/examples/``; the documentation's examples are part of the suite.
- Two test-only Cython extensions (``tests/bake_eval_probe.pyx``,
  ``tests/bake_c_evaluator.pyx``) provide what a ``.py`` file cannot: a
  Cython subclass overriding a ``cdef`` hook, and the parity suite's
  no-Python-in-between walk.

The C suites
------------

Six configurations of the same suites, from ``tests/bake/``:

.. code-block:: bash

    make check        # GCC, default flags
    make asan         # AddressSanitizer + UBSan + LSan - a leak or UB is a failure
    make clang        # Clang
    make lenient      # DCG_VIGILANT=0 - mismatched reads hand back their empty value
    make dispatch     # DCG_EVAL_DIRECT_HOOKS=0 - rules found by dispatch, not installed
    make mutable      # DCG_MAPPING_IMMUTABLE_DTYPE=0 - a store may retype
    make bench        # the eval benchmark, both builds

Each run writes per-suite logs and a ``summary.log`` under
``tests/bake/artifacts/<config>/``; the run ends with ``ALL SUITES PASSED``.
The strict-warning flags matter: the C sources compile under ``-Wall -Wextra
-Werror``, so a warning fails the build rather than scrolling past.

The parity harness
------------------

.. code-block:: bash

    python tests/bake_parity/runner.py

The same graph, built and walked three ways — capi, bake walked from Python,
bake walked in C — each in its own process, each writing a transcript that the
runner compares. It exits non-zero when an arm fails or the arms disagree; the
pytest suite wraps it, so a plain ``pytest tests/`` also runs it. Transcripts
land under ``tests/bake_parity/artifacts/``.

The viewer artifacts
--------------------

.. code-block:: bash

    python tests/bake_webui_artifacts.py

Writes the deep tree and the split build, baked and un-baked, to
``tests/artifacts/bake_webui/`` — the pages the gallery screenshots (and the
gallery tells you how to re-snapshot them).

What runs where
---------------

- **Linux**: everything above.
- **Windows** (via ``nt_build.py`` — see :doc:`development`): the Python suite
  and the compiled extensions, under MSVC. Two things do not run there, by
  design: the C matrix (GNU make) and the fork-based across-process
  reconstruction tests (``tests/test_bake_reconstruct.py``; the shared region
  is only visible to a fork).
- **CI**: on every push — the Python suite on Linux (3.12/3.13/3.14) and
  Windows; the C matrix is a local/development sweep.

Adding to the suites
--------------------

- A new wrapper surface goes in the matching ``tests/test_bake_<module>.py``;
  a new protocol behaviour goes in the next free index under
  ``tests/bake/<protocol>/`` — the numbering is the reading order.
- A change to the C core is not tested until it is tested from *both* sides:
  the C suites recompile the headers, the Python suite runs the built
  extensions. Run both.
