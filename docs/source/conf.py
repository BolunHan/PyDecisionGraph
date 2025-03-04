import os
import sys
sys.path.insert(0, os.path.abspath('../..'))  # Important for module discovery

# Add these extensions
extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.viewcode',
    'sphinx.ext.napoleon',
    'sphinx.ext.githubpages'
]

# Add these at the end
def setup(app):
    from sphinx.builders.html import StandaloneHTMLBuilder
    StandaloneHTMLBuilder.supported_image_types = [
        'image/svg+xml',
        'image/png',
        'image/jpeg'
    ]