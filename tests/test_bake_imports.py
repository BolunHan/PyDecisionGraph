"""Import-order tests for the ``decision_graph.decision_tree.bake`` layer.

Scope is the layer's import graph, not its behaviour: the other bake suites
cover what the wrappers do, this one covers whether they can be reached at all.

The graph may be a DAG and nothing else. Cython turns a ``cimport`` into a
runtime import of the whole module, and it emits one for every ``cdef`` class
declared in a cimported ``.pxd`` - including classes the importing module never
names. A cycle in the ``pxd`` layer is therefore invisible in the source: it
shows up only as an import that fails when it happens to run first, because the
second module to load sees the first one half-built and the class it asks for is
not there yet. Two modules in a cycle import cleanly in one order and raise
``AttributeError: partially initialized module ... (most likely due to a
circular import)`` in the other.

That is why the order is tested rather than assumed. ``c_node`` reaching into
``c_logic_group`` for the manager, with ``c_logic_group`` reaching back through
``c_hierarchy``, looked harmless in every suite that ran - each one entered the
layer through a module that happened to be loadable first.

Oracle: the child interpreter's own exit status. It is derived from nothing the
layer reports, and it cannot pass by accident - importing a module first either
works or it does not.
"""

import os
import subprocess
import sys
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACKAGE = 'decision_graph.decision_tree.bake'

# Every module of the layer; the test is that the order among them does not
# matter. That is all of them now.
MODULES = (
    'c_allocator_protocol',
    'c_var',
    'c_edge',
    'c_const',
    'c_logic_group',
    'c_node',
    'c_action',
    'c_expr',
    'c_hierarchy',
    'c_collections',
    'c_reconstruct',
    'c_bake',
)


def _import_first(statement: str) -> subprocess.CompletedProcess:
    """Run ``statement`` as the first bake import of a fresh interpreter.

    The child inherits this interpreter's import roots, so it resolves the same
    package the parent did whatever the caller's PYTHONPATH was.
    """
    env = os.environ.copy()
    env['PYTHONPATH'] = os.pathsep.join(path for path in sys.path if path)
    return subprocess.run(
        [sys.executable, '-c', statement],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        env=env,
    )


class TestFirstImport(unittest.TestCase):
    """Contract: the layer's runtime import graph is acyclic.

    Expected behavior:
        - every module imports cleanly when it is the first bake import made;
        - a failure names the module, because the ordering is the variable.

    The layer is entered from the outside, so any module may be the one a caller
    reaches for first: ``from ...bake.c_hierarchy import RootLogicNode`` is as
    reasonable an entry point as any other, and it is the one a cycle breaks.
    """

    def test_00_every_module_imports_first(self) -> None:
        """Each module is importable as the very first bake import."""
        for module in MODULES:
            with self.subTest(first=module):
                result = _import_first(f'import {PACKAGE}.{module}')
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_01_a_class_imports_first(self) -> None:
        """A caller may reach for a class rather than for a module.

        The graph's own types are the ones a cycle hits: each class is declared
        in the module a sibling's pxd pulled in, so it is what a half-built
        module gets asked for by name.
        """
        result = _import_first(
            f'from {PACKAGE}.c_hierarchy import RootLogicNode, BreakpointNode'
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_02_the_package_itself_imports(self) -> None:
        """The package imports without pulling its modules in first."""
        result = _import_first(f'import {PACKAGE}')
        self.assertEqual(result.returncode, 0, result.stderr)


class TestPackageExports(unittest.TestCase):
    """Contract: a name a module defines is reachable from the package.

    Expected behavior:
        - the package re-exports each module's public names, so a caller can
          name a type without having to know which module it lives in;
        - the bake protocol's two names are among them - the report a bake
          answers with, and the door it is asked through.

    A wrapper a caller can HOLD but cannot NAME is the failure this catches. A
    root hands back a ``BakeReport`` and a ``BakeFailureError`` carries one, so
    the class has to be importable the way every other wrapper's is - otherwise
    a caller reads a report it cannot catch, annotate or isinstance against.

    The sweep is over each module's own public names rather than over a list
    kept here, so a symbol added to a module without reaching the package is a
    failure rather than something to remember to add.
    """

    def test_00_the_bake_protocol_is_exported(self) -> None:
        """``BakeReport`` and the door are reachable from the package itself."""
        from decision_graph.decision_tree.bake import BakeReport, c_dcg_bake_root
        from decision_graph.decision_tree.bake.c_bake import BakeReport as FromModule

        self.assertIs(BakeReport, FromModule)
        self.assertTrue(callable(c_dcg_bake_root))

    def test_01_every_public_module_name_reaches_the_package(self) -> None:
        """Each module's public names are all attributes of the package."""
        import importlib

        package = importlib.import_module(PACKAGE)
        missing = {}
        for module in MODULES:
            imported = importlib.import_module(f'{PACKAGE}.{module}')
            names = getattr(imported, '__all__', None)
            if names is None:
                names = [name for name in vars(imported) if not name.startswith('_')]
            for name in names:
                if not hasattr(package, name):
                    missing.setdefault(module, []).append(name)

        self.assertEqual(missing, {}, f'names a module defines but the package does not re-export: {missing}')
