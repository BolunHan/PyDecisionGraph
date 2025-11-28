# Sphinx configuration for DecisionGraph docs (minimal, Breathe-enabled)
import os
import sys

# Make project root importable (docs/source -> ../../)
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..')))
try:
    from decision_graph import __version__

    release = f'v{__version__}'
    version = f'v{__version__}'
except ImportError:
    # Fallback if import fails
    release = 'unknown'
    version = 'unknown'

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = 'DecisionGraph'
copyright = '2025, Bolun Han'
author = 'Bolun Han'

extensions = [
    'sphinx.ext.autodoc',
    'breathe',
]

# Breathe configuration - point to Doxygen XML output
breathe_projects = {
    "DecisionGraph API": os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'doxygen', 'xml'))
}

breathe_default_project = "DecisionGraph API"

# Map file extensions from Doxygen to the Python Sphinx domain so Breathe renders Python-style docs
# This helps when Doxygen parsed .py/.pyi/.pyx/.pxd files as Python and you want Sphinx to render them
breathe_domain_by_extension = {
    'py': 'py',
    'pyi': 'py',
}

templates_path = ['_templates']
exclude_patterns = []

html_theme = 'furo'
html_static_path = ['_static']
html_title = f"{project} {release}"

# Furo theme options
html_theme_options = {
    "sidebar_hide_name": False,
    "navigation_with_keys": True,
}
