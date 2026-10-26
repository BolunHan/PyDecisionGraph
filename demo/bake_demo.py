"""The bake layer: build once, bake, then decide.

One of the three protocol demos. The bake layer is the C-first engine: the
same decision is built out of its own classes, baked once (verify, lock,
prepare), and then walked. The store stays live after the bake - feeding it
moves the next walk, while the graph's shape is frozen.

Run it from the repository root::

    python demo/bake_demo.py

It prints the bake report, the decision and the walk, and writes
``bake_demo.html`` next to the working directory.
"""

import logging

# The import banner and the viewer's export line are noise in the demo session.
logging.disable(logging.INFO)

from decision_graph.decision_tree.bake.c_action import LongAction, NoAction, ShortAction  # noqa: E402
from decision_graph.decision_tree.bake.c_collections import LogicMapping  # noqa: E402
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode  # noqa: E402
from decision_graph.webui.bake import to_html  # noqa: E402

logging.disable(logging.NOTSET)
logging.getLogger('DecisionGraph').setLevel(logging.WARNING)

book = LogicMapping(name='demo_bake')

with RootLogicNode(name='Bake Demo') as root:
    with book:
        exposure = book['exposure']
        working = book['working_order']
        up = book['up_prob']
        down = book['down_prob']
        volatility = book['volatility']

        with exposure == 0:
            with volatility > 0.25:
                with down > 0.1:
                    LongAction()

                with up < -0.1:
                    ShortAction()

            with working != 0:
                NoAction()

book.update({
    'exposure': 0,
    'working_order': 0,
    'up_prob': 0.8,
    'down_prob': 0.2,
    'volatility': 0.26,
})

report = root.bake()
print(f'bake: {report.code_name} · {report.nodes} nodes · {report.depth} levels deep')
print(f'decision: {root()}')
print('walk:', ' > '.join(str(node) for node in root.eval_path))
to_html(root, 'bake_demo.html')
print('html: bake_demo.html')
