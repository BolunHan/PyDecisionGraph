import json
import os
import platform
import re
import shutil
import sys
from contextlib import suppress
from pathlib import Path

import cbase
from Cython.Build import cythonize
from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext

# ==============================
# Setup Configuration
# ==============================

BUILD_SCRIPT_VERSION = "1.1.0"
PACKAGE_NAME = "decision_graph"
DISPLAY_NAME = "PyDecisionGraph"

WITH_ANNOTATION = False
NO_MARCH_NATIVE = os.environ.get('GITHUB_ACTIONS') == 'true' or os.environ.get('GITLAB_CI') == 'true'
COMPILE_FLAGS = ["/Ox", "/std:c17", "/experimental:c11atomics"] if platform.system() == "Windows" else ['-O3'] + ([] if NO_MARCH_NATIVE else ['-march=native'])
REPO_ROOT = os.path.abspath(os.path.dirname(__file__))
N_CORES = os.cpu_count() or 1
N_THREADS = 0 if (platform.system() == "Windows" or NO_MARCH_NATIVE) else max(1, N_CORES - 2)
__VERSION__ = match.group(1) if (match := re.search(r'^__version__\s*=\s*["\']([^"\']+)["\']', (Path(REPO_ROOT) / PACKAGE_NAME / '__init__.py').read_text(), re.MULTILINE)) else "unknown"

CBASE_INCLUDE = cbase.get_include()

ext_modules = []
cython_extension = []


def overridable_macros() -> list:
    """Every macro a build may flip from the environment: the probe's inventory.

    ``probe.py`` writes ``macros.json`` — the ``#define``s the C layer's own
    headers declare, each with its default. A name in that inventory (and
    ``DEBUG``) is overridable with an environment variable of the same name —
    ``DCG_VIGILANT=0 python setup.py build_ext --inplace`` — and a macro left
    unset keeps its header default. The inventory is a cache, regenerated with
    ``python probe.py``; with none present only ``DEBUG`` is overridable.
    """
    names = ["DEBUG"]
    inventory = Path(REPO_ROOT) / "macros.json"
    if inventory.exists():
        try:
            names += [
                m["name"]
                for m in json.loads(inventory.read_text(encoding="utf-8")).get("macros", [])
            ]
        except (OSError, ValueError) as exc:
            print(f"[build_py] Warning: macros.json unreadable ({exc}) - only DEBUG is overridable")
    return names


# ==============================
# Custom Build Extension Class
# ==============================


class BuildExtWithConfig(build_ext):
    __cy_modules__ = [
        "decision_graph.decision_tree.bake",
    ]

    def initialize_options(self):
        super().initialize_options()
        self.parallel = N_THREADS

    def run(self):
        self.pre_compile()

        super().run()

        self.post_compile()
        print(f"[build_py] <{DISPLAY_NAME}> v{__VERSION__} setup complete. Built {len(self.extensions)} Cython extensions.")

    def build_extensions(self):
        macros = []
        for macro in overridable_macros():
            val = os.environ.get(macro)
            if val:
                print(f'[build_py] Compile-time variable {macro} overridden with value {val}')
                macros.append((macro, val))
        for ext in self.extensions:
            ext.define_macros = macros
        super().build_extensions()

    def pre_compile(self):
        print(f"[build_py] setup.py v{BUILD_SCRIPT_VERSION}")
        self.collect_sources()

    def post_compile(self):
        # Monkey hack the "__init__.pxd" issue:
        self.inject_pxd()

        # Inject the generated includes/ mirror into build_lib so it gets packaged
        self.inject_sources()

    @classmethod
    def collect_sources(cls) -> None:
        project_root = Path(__file__).resolve().parent
        source_root = project_root / PACKAGE_NAME
        include_root = project_root / PACKAGE_NAME / "includes"
        mirror_root = include_root / PACKAGE_NAME

        if mirror_root.exists():
            shutil.rmtree(mirror_root)

        copied = 0
        source_patterns = ["*.h", "*.c", "*.cpp"]
        for pattern in source_patterns:
            for source_file in sorted(source_root.rglob(pattern)):
                # Skip files inside the generated include_root
                if include_root in source_file.parents:
                    continue
                dest = include_root.joinpath(*source_file.relative_to(project_root).parts)
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source_file, dest)
                copied += 1

        print(f"[build_py] <{DISPLAY_NAME}> mirrored {copied} C source file(s) -> {include_root.relative_to(project_root)}")

    def inject_sources(self) -> None:
        project_root = Path(__file__).resolve().parent
        include_root = project_root / PACKAGE_NAME / "includes"
        mirror_root = include_root / PACKAGE_NAME

        if not mirror_root.exists():
            return

        dest_root = Path(self.build_lib, PACKAGE_NAME, "includes", PACKAGE_NAME)
        if dest_root.exists():
            shutil.rmtree(dest_root)

        shutil.copytree(mirror_root, dest_root)
        print(f"[build_py] <{DISPLAY_NAME}> injected includes mirror -> {dest_root.relative_to(Path(self.build_lib))}")

    @classmethod
    def remove_pxd(cls) -> None:
        project_root = Path(__file__).resolve().parent

        for module in cls.__cy_modules__:
            src_dir = project_root.joinpath(*module.split("."))
            init_pxd = src_dir / "__init__.pxd"

            if init_pxd.exists():
                print(f"[build_py] [pre_compile] Removing {init_pxd}")
                with suppress(FileNotFoundError):
                    init_pxd.unlink()

        cls._pxd_backup = []
        try:
            import Cython.Includes as _cy_includes
            includes_dir = Path(next(iter(_cy_includes.__path__)))
            pkg_link = includes_dir / PACKAGE_NAME
            if pkg_link.exists():
                backup = pkg_link.with_name(f"{PACKAGE_NAME}.cy_bak")
                print(f"[build_py] [pre_compile] Moving Cython/Includes symlink {pkg_link.name} -> {backup.name}")
                pkg_link.rename(backup)
                cls._pxd_backup.append((backup, pkg_link))
        except Exception as e:
            print(f"[build_py] [pre_compile] Warning: Could not handle Cython/Includes symlink: {e}")

    @classmethod
    def restore_pxd(cls) -> None:
        """Restore symlink in Cython/Includes after cythonize."""
        for backup, original in getattr(cls, '_pxd_backup', []):
            if backup.exists():
                print(f"[build_py] [pre_compile] Restoring Cython/Includes symlink {backup.name} -> {original.name}")
                backup.rename(original)
        cls._pxd_backup = []

    def inject_pxd(self) -> None:
        project_root = Path(__file__).resolve().parent

        for module in self.__cy_modules__:
            src_dir = project_root.joinpath(*module.split("."))
            pkg_dir = Path(self.build_lib, *module.split("."))

            infra_pxd = src_dir / "__infra__.pxd"
            if not infra_pxd.exists():
                continue

            # Inject into build_lib (for wheel / deploy)
            pkg_dir.mkdir(parents=True, exist_ok=True)
            init_pxd = pkg_dir / "__init__.pxd"
            print(f"[build_py] [post_compile] Injecting {infra_pxd} -> {init_pxd}")
            shutil.copyfile(infra_pxd, init_pxd)

            # Also restore into src_dir (so source tree keeps __init__.pxd)
            src_init_pxd = src_dir / "__init__.pxd"
            if not src_init_pxd.exists():
                print(f"[build_py] [post_compile] Restoring {infra_pxd} -> {src_init_pxd}")
                shutil.copyfile(infra_pxd, src_init_pxd)


# =============================
# Define Cython Extensions
# =============================

cython_extension.extend([
    Extension(
        name="decision_graph.decision_tree.capi.c_abc",
        sources=["decision_graph/decision_tree/capi/c_abc.pyx"],
        extra_compile_args=COMPILE_FLAGS,
        include_dirs=[REPO_ROOT, *CBASE_INCLUDE]
    ),
    Extension(
        name="decision_graph.decision_tree.capi.c_collection",
        sources=["decision_graph/decision_tree/capi/c_collection.pyx"],
        extra_compile_args=COMPILE_FLAGS,
        include_dirs=[REPO_ROOT, *CBASE_INCLUDE]
    ),
    Extension(
        name="decision_graph.decision_tree.capi.c_node",
        sources=["decision_graph/decision_tree/capi/c_node.pyx"],
        extra_compile_args=COMPILE_FLAGS,
        include_dirs=[REPO_ROOT, *CBASE_INCLUDE]
    ),
    Extension(
        name="decision_graph.decision_tree.bake.c_allocator_protocol",
        sources=["decision_graph/decision_tree/bake/c_allocator_protocol.pyx"],
        extra_compile_args=COMPILE_FLAGS,
        include_dirs=[REPO_ROOT, *CBASE_INCLUDE]
    ),
    Extension(
        name="decision_graph.decision_tree.bake.c_var",
        sources=["decision_graph/decision_tree/bake/c_var.pyx"],
        extra_compile_args=COMPILE_FLAGS,
        include_dirs=[REPO_ROOT, *CBASE_INCLUDE]
    ),
    Extension(
        name="decision_graph.decision_tree.bake.c_edge",
        sources=["decision_graph/decision_tree/bake/c_edge.pyx"],
        extra_compile_args=COMPILE_FLAGS,
        include_dirs=[REPO_ROOT, *CBASE_INCLUDE]
    ),
    Extension(
        name="decision_graph.decision_tree.bake.c_logic_group",
        sources=["decision_graph/decision_tree/bake/c_logic_group.pyx"],
        extra_compile_args=COMPILE_FLAGS,
        include_dirs=[REPO_ROOT, *CBASE_INCLUDE]
    ),
    Extension(
        name="decision_graph.decision_tree.bake.c_node",
        sources=["decision_graph/decision_tree/bake/c_node.pyx"],
        extra_compile_args=COMPILE_FLAGS,
        include_dirs=[REPO_ROOT, *CBASE_INCLUDE]
    ),
    Extension(
        name="decision_graph.decision_tree.bake.c_action",
        sources=["decision_graph/decision_tree/bake/c_action.pyx"],
        extra_compile_args=COMPILE_FLAGS,
        include_dirs=[REPO_ROOT, *CBASE_INCLUDE]
    ),
    Extension(
        name="decision_graph.decision_tree.bake.c_expr",
        sources=["decision_graph/decision_tree/bake/c_expr.pyx"],
        extra_compile_args=COMPILE_FLAGS,
        include_dirs=[REPO_ROOT, *CBASE_INCLUDE]
    ),
    Extension(
        name="decision_graph.decision_tree.bake.c_const",
        sources=["decision_graph/decision_tree/bake/c_const.pyx"],
        extra_compile_args=COMPILE_FLAGS,
        include_dirs=[REPO_ROOT, *CBASE_INCLUDE]
    ),
    Extension(
        name="decision_graph.decision_tree.bake.c_hierarchy",
        sources=["decision_graph/decision_tree/bake/c_hierarchy.pyx"],
        extra_compile_args=COMPILE_FLAGS,
        include_dirs=[REPO_ROOT, *CBASE_INCLUDE]
    ),
    Extension(
        name="decision_graph.decision_tree.bake.c_collections",
        sources=["decision_graph/decision_tree/bake/c_collections.pyx"],
        extra_compile_args=COMPILE_FLAGS,
        include_dirs=[REPO_ROOT, *CBASE_INCLUDE]
    ),
    Extension(
        name="decision_graph.decision_tree.bake.c_reconstruct",
        sources=["decision_graph/decision_tree/bake/c_reconstruct.pyx"],
        extra_compile_args=COMPILE_FLAGS,
        include_dirs=[REPO_ROOT, *CBASE_INCLUDE]
    ),
    Extension(
        name="decision_graph.decision_tree.bake.c_bake",
        sources=["decision_graph/decision_tree/bake/c_bake.pyx"],
        extra_compile_args=COMPILE_FLAGS,
        include_dirs=[REPO_ROOT, *CBASE_INCLUDE]
    ),
    # A test-only module: the Cython half of the eval-hook tests needs a Cython
    # subclass to override a `cdef` hook, which a .py test file cannot express.
    Extension(
        name="tests.bake_eval_probe",
        sources=["tests/bake_eval_probe.pyx"],
        extra_compile_args=COMPILE_FLAGS,
        include_dirs=[REPO_ROOT, *CBASE_INCLUDE]
    ),
    # A test-only module: the parity suite's "walked in C" arm needs a door that
    # runs the walk without Python in between, which a .py test file cannot be.
    Extension(
        name="tests.bake_c_evaluator",
        sources=["tests/bake_c_evaluator.pyx"],
        extra_compile_args=COMPILE_FLAGS,
        include_dirs=[REPO_ROOT, *CBASE_INCLUDE]
    ),
])

if __name__ == "__main__":
    BuildExtWithConfig.remove_pxd()

    try:
        ext_modules.extend(
            cythonize(
                cython_extension,
                annotate=WITH_ANNOTATION,
                compiler_directives={
                    "language_level": "3",
                    'embedsignature': True
                },
                force="--force" in sys.argv,
                nthreads=N_THREADS,
            )
        )
    finally:
        BuildExtWithConfig.restore_pxd()

    # =============================
    # Setup Function
    # =============================

    setup(
        name=DISPLAY_NAME,
        ext_modules=ext_modules,
        cmdclass={"build_ext": BuildExtWithConfig},
    )
