"""Bake a built graph once, then decide in a hot loop.

The bake layer's shape: build a graph out of its own classes, bake it once with
``root.bake()`` — verifying it, locking it against structural change, and
preparing the record a walk fills — then evaluate it as often as the loop
needs. The bake is all-or-nothing: it either prepares the whole graph or
changes nothing.

The store stays live after the bake: feeding it moves the next walk.

Run it with the package installed, from any directory::

    python bake_hot_loop.py
"""

from decision_graph.decision_tree.bake.c_action import LongAction, ShortAction
from decision_graph.decision_tree.bake.c_collections import LogicMapping
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode

book = LogicMapping(name='book')

with RootLogicNode(name='Hot Loop') as root:
    with book:
        exposure = book['exposure']
        up = book['up_prob']
        down = book['down_prob']

        with exposure > ConstantNode(0):
            with up > ConstantNode(0.5):
                LongAction()

            with down > ConstantNode(0.5):
                ShortAction()

# Feed the store, then bake what was built.
book['exposure'] = 2
book['up_prob'] = 0.7
book['down_prob'] = 0.1

report = root.bake()
print(f'bake: {report.code_name} · {report.nodes} nodes · {report.depth} levels deep · locked {report.locked}')

# The prepared graph answers a walk without preparing anything.
decision = root()
print(f'decision: {decision}')
print('walk:', ' > '.join(str(node) for node in root.eval_path))

# The store is still live: a new feed moves the next walk.
book.update({'exposure': -1, 'down_prob': 0.8})
print(f'refed decision: {root()}')
