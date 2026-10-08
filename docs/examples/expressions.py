"""Conditions compose: comparisons, combinations, and arithmetic on reads.

A read (``book.exposure``) is a node of the graph, not a Python value: comparing
it builds a comparison node, ``&`` and ``|`` combine conditions into their own
nodes, and arithmetic between reads is an expression node. The walk evaluates
them as it reaches them, and the store moves the answer.

Run it with the package installed, from any directory::

    python expressions.py
"""

from decision_graph.decision_tree import LongAction, ShortAction, RootLogicNode, LogicMapping

state = {
    "exposure": 2,
    "down_prob": 0.15,
    "up_prob": 0.85,
    "volatility": 0.30,
}

with LogicMapping(name='book', data=state) as book:
    with RootLogicNode(name='Expressions') as root:
        with book.exposure > 0:
            # Two conditions combined into one node: both must hold.
            with (book.down_prob > 0.1) & (book.volatility > 0.25):
                ShortAction()

            # Arithmetic between two reads, compared against a literal.
            with book.exposure + book.up_prob > 1.0:
                LongAction()

feeds = [
    {"exposure": 2, "down_prob": 0.15, "up_prob": 0.85, "volatility": 0.30},
    {"exposure": 2, "down_prob": 0.05, "up_prob": 0.85, "volatility": 0.10},
]

for feed in feeds:
    book.update(feed)
    print(f"{feed} -> {root()}")
    print("  walk:", " > ".join(str(node) for node in root.eval_path))
