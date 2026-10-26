"""The native layer: the same decision, in pure Python.

One of the three protocol demos. The model is the quick start's - a store of
named values, a root, and the conditions that decide an action - but this
script imports ``decision_graph.decision_tree.native`` by name, so it runs
whether or not the compiled extensions are built.

Run it from the repository root::

    python demo/native_demo.py

It prints the decision and the walk, and writes ``native_demo.html`` next to
the working directory.
"""

import logging

# The import banner and the viewer's export line are noise in the demo session.
logging.disable(logging.INFO)

from decision_graph.decision_tree.native import (  # noqa: E402
    LogicMapping,
    LongAction,
    NoAction,
    RootLogicNode,
    ShortAction,
)
from decision_graph.webui.native import to_html  # noqa: E402

logging.disable(logging.NOTSET)
logging.getLogger('DecisionGraph').setLevel(logging.WARNING)

state = {
    "exposure": 0,
    "working_order": 0,
    "up_prob": 0.8,
    "down_prob": 0.2,
    "volatility": 0.26,
}

with LogicMapping(name='demo_native', data=state) as book:
    with RootLogicNode(name='Native Demo') as root:
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
to_html(root, 'native_demo.html')
print('html: native_demo.html')
