Installation
============

Requirements
------------

- **Python 3.12 or newer** (the packages ship wheels for CPython 3.12+ on
  Linux and Windows; the test suite runs on both).
- **PyCyBase** — the allocator/container layer every compiled module in this
  package links against. It is declared as a build requirement, so ``pip``
  fetches it automatically for a source build; a wheel install should carry it
  explicitly (see below).
- **A C toolchain** only when building from source: GCC or Clang on Linux,
  MSVC on Windows (the CI builds use exactly what the platform ships).

From PyPI
---------

.. code-block:: bash

    pip install PyDecisionGraph

For the session-time payload types (a ``PyAlgoEngine`` integration used by
``decision_graph.logic_group`` and the ``TIME`` / ``DATE`` / ``DATETIME`` value
tags), install the extra as well:

.. code-block:: bash

    pip install "PyDecisionGraph[session_time_support]"

For the web viewers, install the visualization extra:

.. code-block:: bash

    pip install "PyDecisionGraph[visualization]"

``pip show PyDecisionGraph`` names the installed version; the version is also
in ``decision_graph.__version__``.

From source
-----------

.. code-block:: bash

    git clone https://github.com/BolunHan/PyDecisionGraph.git
    cd PyDecisionGraph
    pip install -r requirements.txt        # PyCyBase, PyAlgoEngine, pytest, flask, jinja2
    python setup.py build_ext --inplace    # builds the 17 Cython extensions in place

There are convenience scripts: ``build.sh`` (POSIX) and ``build.ps1``
(Windows) clean and rebuild; both are wrappers around
``setup.py build_ext --inplace --force``. See :doc:`../contributing/development`
for the build system's details — in particular, the *clean rebuild* recipe that
a header change requires.

Checking the install
--------------------

.. code-block:: python

    import decision_graph
    print(decision_graph.__version__)
    print(decision_graph.get_include())     # where the C headers and mirrors live

That import also loads the compiled layer (``decision_graph.decision_tree.capi``
when its extensions are available, otherwise the pure-Python fallback under
``.native``). Which one you got is readable at runtime:

.. code-block:: python

    from decision_graph.decision_tree import USING_CAPI
    print('compiled layer active:', USING_CAPI)
