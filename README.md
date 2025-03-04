# PyDecisionGraph

`PyDecisionGraph` is an easy-to-use library to create custom decision trees, primarily designed for trading and financial decision-making. This package helps you build rule-based decision processes and visualize them effectively.

## Installation

To install the package, run:

```bash
pip install PyDecisionGraph
```

# Requirements

- Python 3.12 or higher

# Documentation

For detailed documentation, visit https://pydecisiongraph.readthedocs.io/.

# Quick Start

Here is a quick demo on how to use `PyDecisionGraph` for building a decision tree based on various conditions:

```python
from decision_graph.decision_tree import LogicNode, LOGGER, AttrExpression, LongAction, ShortAction, NoAction, RootLogicNode, LogicMapping

# Mapping of attribute names to their values
LogicMapping.AttrExpression = AttrExpression

state = {
    "exposure": 0,  # Current exposure
    "working_order": 0,  # Current working order
    "up_prob": 0.8,  # Probability of price going up
    "down_prob": 0.2,  # Probability of price going down
    "volatility": 0.24,  # Current market volatility
    "ttl": 15.3  # Time to live (TTL) of the decision tree
}

# Root of the logic tree
with RootLogicNode() as root:
    # Define root logic mapping with state data
    with LogicMapping(name='Root', data=state) as lg_root:
        lg_root: LogicMapping

        # Condition for zero exposure
        with lg_root.exposure == 0:
            root: LogicNode
            with LogicMapping(name='check_open', data=state) as lg:
                with lg.working_order != 0:
                    break_point = NoAction()  # No action if there's a working order
                    lg.break_(scope=lg)  # Exit the current scope

                with lg.volatility > 0.25:  # Check if volatility is high
                    with lg.down_prob > 0.1:  # Action for down probability
                        LongAction()

                    with lg.up_prob < -0.1:  # Action for up probability
                        ShortAction()

        # Condition when TTL is greater than 30
        with lg_root.ttl > 30:
            with lg_root.working_order > 0:
                ShortAction()  # Action to short if working order exists
            LongAction()  # Always take long action
            lg_root.break_(scope=lg_root)  # Exit scope

        # Closing logic based on exposure and probabilities
        with LogicMapping(name='check_close', data=state) as lg:
            with (lg.exposure > 0) & (lg.down_prob > 0.):
                ShortAction()  # Short action for positive exposure and down probability

            with (lg.exposure < 0) & (lg.up_prob > 0.):
                LongAction()  # Long action for negative exposure and up probability

# Visualize the decision tree
root.to_html()

# Log the evaluation result
LOGGER.info(root())
```

## Explanation of the Script:

- LogicNode & LogicMapping:
    - LogicNode: Represents a node in the decision tree where conditions are evaluated.
    - LogicMapping: Associates logical conditions with the state (data) used in decision-making.

- State:
    - A dictionary containing the variables used for decision-making, such as exposure, working_order, up_prob, etc.

- RootLogicNode:
    - The entry point for the decision tree where all logical decisions are linked.

- Decision Conditions:
    - Inside each with block, logical conditions are evaluated (e.g., lg.volatility > 0.25, lg.up_prob < -0.1) to determine which action to take.
    - Actions like LongAction() or ShortAction() are taken based on the conditions.

- Action Handling:
    - LongAction(), ShortAction(), and NoAction() represent different actions you can trigger in the decision tree based on the conditions.

- Logging:
    - The result of the tree evaluation is logged using the LOGGER object, which outputs to the console.

- Visualization:
    - root.to_html() generates an HTML representation of the decision tree for visualization.

# Features

- Easily define custom decision rules.
- Actionable outcomes like LongAction, ShortAction, and NoAction.
- Log outputs for debugging and tracking.
- Visualize decision paths through HTML export.

---

# Contributing

Feel free to fork, submit issues, and create pull requests. Contributions are always welcome!

# License

This project is licensed under the Mozilla Public License 2.0 - see the LICENSE file for details.