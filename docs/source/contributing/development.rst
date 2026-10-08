Development
===========

Everything needed to build, rebuild and ship this repository — stated so it
can be executed, not just read. The map of the code is
:doc:`../usage/architecture`; the suites are :doc:`testing`.

Building
--------

.. code-block:: bash

    pip install -r requirements.txt          # PyCyBase, PyAlgoEngine, pytest, flask, jinja2
    python setup.py build_ext --inplace      # 17 extensions, in place

The wrappers do a clean + forced rebuild: ``./build.sh`` (POSIX) and
``.\build.ps1`` (Windows). Both end with a line like
``Built 17 Cython extensions`` — that line is the check that the build
actually happened.

**A header change requires a clean rebuild.** The C core is header-only, so a
changed ``.h`` neither regenerates a ``.pyx``'s C nor relinks an object: the
compiled extensions would keep running the old header while the C suites (which
recompile headers every run) see the new one. After touching any ``.h``:

.. code-block:: bash

    rm -f decision_graph/decision_tree/bake/*.c decision_graph/decision_tree/capi/*.c
    rm -rf build/temp.*/decision_graph
    python setup.py build_ext --inplace

Compile-time macros
-------------------

``probe.py`` scans the pxds' extern headers for the ``#define`` macros and
writes ``macros.json`` — the inventory of what a build can flip:

.. code-block:: bash

    python probe.py          # writes macros.json (a cache; regenerate when headers change)

``setup.py`` reads that inventory, so any probed macro (and ``DEBUG``) is
overridable from the environment, e.g. a lenient transport build:

.. code-block:: bash

    DCG_VIGILANT=0 python setup.py build_ext --inplace

Layout rules that are not negotiable
------------------------------------

- ``decision_graph/includes/`` and every ``__init__.pxd`` are **generated** by
  the build (from the headers, and from ``__infra__.pxd``). Edit the sources,
  never the mirrors.
- C and Cython **sources are ASCII**. MSVC compiles under the system code
  page; a non-ASCII literal breaks or corrupts the Windows build. (The tree
  renderer's box-drawing glyphs are UTF-8 byte escapes for this reason.)
- The pxd/header ownership and hub rules are on
  :doc:`../usage/architecture`; ``DEPENDENCY.md`` in the bake directory is the
  design document.

The test suites
---------------

See :doc:`testing` for the full map. The short version:

.. code-block:: bash

    python -m pytest tests/ -q                      # the Python suite
    make -C tests/bake check                        # the C suites (GCC default)
    python tests/bake_parity/runner.py              # the three-arm parity harness

Windows (the NT VM)
-------------------

The Windows workflow syncs the repository to a VM, builds it there with MSVC,
and runs the suite — the same steps this repository's CI uses on its Windows
runner:

.. code-block:: bash

    python nt_build.py                  # full cycle: sync -> build (build.ps1) -> test
    python nt_build.py --sync-only      # just sync
    python nt_build.py --step ensure_deps   # provisioning steps for the VM venv
    python nt_build.py --step install       # pip install the tree into the VM venv

- ``nt_config.json`` carries the VM's connection, paths and sync excludes.
- ``build.ps1`` is the remote-side build; ``nt_steps.ps1`` is the remote-side
  step driver (``info | install | ensure_pytest | ensure_deps | test_src |
  test_installed``). Both are invoked with an explicit ``-Config``.
- The VM's ``cbase`` and ``algo_engine`` must be current: this package builds
  against the installed PyCyBase, and the suite imports PyAlgoEngine. After
  changing either, sync + rebuild + clean-reinstall it in the VM venv before
  rebuilding here.

A build is considered clean when it prints no warnings. Compiler warnings from
this repository's sources are defects, not noise.

CI
--

- **GitLab** (``.gitlab-ci.yml``): ``test:linux`` (Python 3.12/3.13/3.14),
  ``test:windows``, ``docs:build`` + ``pages`` (doxygen + sphinx), and
  ``publish:posix`` / ``publish:nt`` (cibuildwheel → twine) on ``v*`` tags.
  ``GITLAB_CI=true`` is predefined, and ``setup.py`` drops ``-march=native``
  when it is set.
- **GitHub** (``.github/workflows/publish_pypi.yml``): wheel building and the
  PyPI upload, triggered by a ``v*.*.*`` tag.

Releases
--------

The version lives in one place — ``decision_graph/__init__.py``
(``__version__``). ``pyproject.toml`` reads it dynamically, ``setup.py``
re-reads it for its banner, and the docs' ``conf.py`` shows it. Bump that one
line, commit, tag ``vX.Y.Z``, push the tag; the pipeline publishes.
