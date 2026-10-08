"""The quick start, verbatim from the project README.

Run it with the package installed, from any directory::

    python quickstart.py

It writes ``tree.html`` next to the working directory (open it in a browser),
and logs the decision the tree made.
"""

from decision_graph.decision_tree import LOGGER, LongAction, ShortAction, NoAction, RootLogicNode, LogicMapping

# Mapping of attribute names to their values
state = {
    "exposure": 0,  # Current exposure
    "working_order": 0,  # Current working order
    "up_prob": 0.8,  # Probability of price going up
    "down_prob": 0.2,  # Probability of price going down
    "volatility": 0.26,  # Current market volatility
    "ttl": 15.3  # Time to live (TTL) of the decision tree
}

# The store the conditions read from, entered for the scope the tree is built in
with LogicMapping(name='Root', data=state) as lg:
    # Root of the logic tree
    with RootLogicNode() as root:
        # Condition for zero exposure
        with lg.exposure == 0:
            with lg.volatility > 0.25:  # Check if volatility is high
                with lg.down_prob > 0.1:  # Action for down probability
                    LongAction()

                with lg.up_prob < -0.1:  # Action for up probability
                    ShortAction()

            # No action if there's a working order
            with lg.working_order != 0:
                NoAction()

# Visualize the decision tree
root.to_html('tree.html')

# Log the evaluation result
LOGGER.info(root())
