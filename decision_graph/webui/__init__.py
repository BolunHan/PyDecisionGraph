"""The web UIs — one per API surface.

Each layer gets its own sub-package and reads that layer directly:

- ``.capi``    ``decision_graph.decision_tree.capi``   — the Cython C API
- ``.native``  ``decision_graph.decision_tree.native`` — the pure-Python model
- ``.bake``    ``decision_graph.decision_tree.bake``   — the bake layer

Nothing is imported here on purpose: a UI drags in Flask and a directory of
browser assets, and importing this package is not asking for either. Reach for
the layer you want, by name::

    from decision_graph.webui.bake import to_html

The three share no code, also on purpose. A visualiser is worth reading only if
it shows what ITS layer actually built, and a helper shared between two layers
is the first place a real difference between them gets papered over.
"""

import logging

# The layer's logger hangs off the root one, so a consumer that re-points
# `decision_graph.LOGGER` re-points every UI with it.
LOGGER = logging.getLogger("DecisionGraph").getChild("WebUI")
