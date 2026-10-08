"""Export a tree through the pure-Python fallback's viewer.

The same model, built with the ``native`` layer's classes and drawn by
``decision_graph.webui.native`` — the gallery's native snapshot comes from this
script. The tree is the quick start's, so the two pages can be compared side by
side.

Run it with the package installed, from any directory::

    python native_export.py

It writes ``native_viewer.html`` next to the working directory.
"""

from decision_graph.decision_tree.native import LongAction, ShortAction, NoAction, RootLogicNode, LogicMapping
from decision_graph.webui.native import to_html

state = {
    "exposure": 0,
    "working_order": 0,
    "up_prob": 0.8,
    "down_prob": 0.2,
    "volatility": 0.26,
    "ttl": 15.3,
}

with LogicMapping(name='native_export', data=state) as lg:
    with RootLogicNode(name='Entry') as root:
        with lg.exposure == 0:
            with lg.volatility > 0.25:
                with lg.down_prob > 0.1:
                    LongAction()

                with lg.up_prob < -0.1:
                    ShortAction()

            with lg.working_order != 0:
                NoAction()

to_html(root, 'native_viewer.html')
print('native export written: native_viewer.html')
