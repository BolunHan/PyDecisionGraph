import os
import sys

sys.path.insert(0, os.path.abspath('../..'))  # Important for module discovery

# Import the version from decision_tree/__init__.py
from decision_tree import __version__

# Project information
project = 'PyDecisionGraph'
author = 'Bolun'
release = __version__

# Add these extensions
extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.viewcode',
    'sphinx.ext.napoleon',
    'sphinx.ext.githubpages'
]

autodoc_default_options = {
    'members': True,
    'member-order': 'bysource',
    'special-members': '__init__',
    'undoc-members': True,
    'exclude-members': '__weakref__'
}

templates_path = ['_templates']
exclude_patterns = []

html_theme = 'sphinx_rtd_theme'
html_static_path = ['_static']


# Add these at the end
def setup(app):
    from sphinx.builders.html import StandaloneHTMLBuilder
    StandaloneHTMLBuilder.supported_image_types = [
        'image/svg+xml',
        'image/png',
        'image/jpeg'
    ]
