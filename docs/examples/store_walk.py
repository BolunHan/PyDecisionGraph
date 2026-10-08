"""Build the graph once; the store moves the decision.

Every read in the graph answers LIVE out of the store, so feeding the store a
new value is what moves the next walk — the tree is built once and asked three
times, and each walk lands somewhere else. The walk it took is on the root's
``eval_path``.

Run it with the package installed, from any directory::

    python store_walk.py
"""

from decision_graph.decision_tree import LongAction, ShortAction, RootLogicNode, LogicMapping

state = {
    "volatility": 0.30,
    "down_prob": 0.15,
    "up_prob": 0.85,
}

with LogicMapping(name='book', data=state) as book:
    with RootLogicNode(name='Store Walk') as root:
        with book.volatility > 0.25:
            with book.down_prob > 0.1:
                ShortAction()

            with book.up_prob > 0.5:
                LongAction()

feeds = [
    {"volatility": 0.30, "down_prob": 0.15, "up_prob": 0.85},  # the market is moving
    {"volatility": 0.30, "down_prob": 0.05, "up_prob": 0.85},  # ... but down_prob is small
    {"volatility": 0.10, "down_prob": 0.15, "up_prob": 0.85},  # the market is quiet
]

for feed in feeds:
    book.update(feed)
    decision = root()
    print(f"{feed} -> {decision}")
    print("  walk:", " > ".join(str(node) for node in root.eval_path))
