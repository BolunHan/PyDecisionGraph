import os
from pathlib import Path

from Cython.Build import cythonize
from setuptools import setup, Extension


def read(rel_path):
    here = Path(__file__).resolve().parent
    return (here / rel_path).read_text(encoding="utf-8")


def get_version(rel_path):
    for line in read(rel_path).splitlines():
        if line.startswith('__version__'):
            delim = '"' if '"' in line else "'"
            return line.split(delim)[1]
    raise RuntimeError("Unable to find version string.")


cython_extension = [
    Extension(
        name="decision_graph.decision_tree.capi.c_abc",
        sources=["decision_graph/decision_tree/capi/c_abc.pyx"],
        # define_macros=[("CYTHON_TRACE", "1"), ('CYTHON_USE_SYS_MONITORING', '1'), ('CYTHON_TRACE_NOGIL', '1')],
    ),
    Extension(
        name="decision_graph.decision_tree.capi.c_collection",
        sources=["decision_graph/decision_tree/capi/c_collection.pyx"],
    ),
    Extension(
        name="decision_graph.decision_tree.capi.c_node",
        sources=["decision_graph/decision_tree/capi/c_node.pyx"],
    ),
]

ext_modules = cythonize(cython_extension, compiler_directives={"language_level": "3"})

setup(
    name="PyDecisionGraph",
    ext_modules=ext_modules,
)
