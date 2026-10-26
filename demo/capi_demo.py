"""The capi layer: the same decision, through the compiled layer.

One of the three protocol demos. The model is the quick start's, built with
``decision_graph.decision_tree.capi`` by name so this demo exercises the
compiled layer even when the public import would have selected the native
fallback.

The compiled extensions must be built (``python setup.py build_ext
--inplace``) for this script to import.

Run it from the repository root::

    python demo/capi_demo.py

It prints the decision and the walk, and writes ``capi_demo.html`` next to the
working directory.
"""

import logging

# The import banner and the viewer's export line are noise in the demo session.
logging.disable(logging.INFO)

from decision_graph.decision_tree.capi import (  # noqa: E402
    LogicMapping,
    LongAction,
    NoAction,
    RootLogicNode,
    ShortAction,
)
from decision_graph.webui.capi import to_html  # noqa: E402

logging.disable(logging.NOTSET)
logging.getLogger('DecisionGraph').setLevel(logging.WARNING)

state = {
    "exposure": 0,
    "working_order": 0,
    "up_prob": 0.8,
    "down_prob": 0.2,
    "volatility": 0.26,
}

with LogicMapping(name='demo_capi', data=state) as book:
    with RootLogicNode(name='Capi Demo') as root:
        with book.exposure == 0:
            with book.volatility > 0.25:
                with book.down_prob > 0.1:
                    LongAction()

                with book.up_prob < -0.1:
                    ShortAction()

            with book.working_order != 0:
                NoAction()

print(f'decision: {root()}')
print('walk:', ' > '.join(str(node) for node in root.eval_path))
to_html(root, 'capi_demo.html')
print('html: capi_demo.html')
