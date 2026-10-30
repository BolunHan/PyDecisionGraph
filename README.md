# PyDecisionGraph

`PyDecisionGraph` is an easy-to-use library to create custom decision trees, primarily designed for trading and financial decision-making. This package helps you build rule-based decision processes and visualize them effectively.

## Layers

- `decision_graph.decision_tree` - the public tree API used in the Quick Start.
  It uses the C API when the extensions are available (`USING_CAPI`) and falls
  back to the pure-Python model otherwise.
- `decision_graph.decision_tree.bake` - the C-first bake engine underneath. Its
  CCP-bound wrappers hold a reference on their C block: when a block is
  released, any other wrapper over that block becomes a husk instead of reading
  freed memory. Its C diagnostics name the operation, the cause, and the
  consequence.

## Installation

To install the package, run:

```bash
pip install PyDecisionGraph
```

## Cython headers

The package ships its Cython sources (`.pxd`, `.pyx`) and the C headers they
bind. When Cython is installed in the same environment, the build links
`decision_graph` into `Cython/Includes` through `cbase.cython_injection`, so
downstream projects can `cimport decision_graph`.

`decision_graph.get_include()` returns every Cython source directory: the
package root, `decision_tree`, `decision_tree/bake`, `decision_tree/capi`, and
the packaged `includes/` mirror when it is present.

## What's new in 0.3.3

- **CCP-bound bake wrappers.** Each CCP-bound bake wrapper holds a reference on
  its C block. When the block is released - by the wrapper that owns it or by
  the C layer during consolidation - every other wrapper over that block
  becomes a husk: guarded accessors refuse instead of reading freed memory, and
  `repr` still answers for the debugger. `__dealloc__` owns the block free;
  `__ccp_dealloc__` resets the wrapper's extra fields, and derived hooks chain.
- **Clearer diagnostics.** C-side failure reports name the operation, the
  `DCG_ERR_*` cause, and the state left behind. The placeholder-consolidation
  report no longer says "no room".
- **Slot preservation.** Unlink, replace, and placeholder consolidation keep a
  node in the same arm instead of moving it to the end of the parent.
- **Packaging.** `get_include()` returns all Cython source directories, and the
  Cython sources ship with the package.
- **Tests.** New coverage for consolidation, logic-group lifetime, and node
  lifecycle, alongside the 12 C suites.

# Requirements

- Python 3.12 or higher
- Building from source: a C compiler and Cython 3.x

# Documentation

For detailed documentation, visit https://pydecisiongraph.readthedocs.io/.

# Quick Start

Here is a quick demo on how to use `PyDecisionGraph` for building a decision tree based on various conditions:

```python
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
```

## Explanation of the Script:

- LogicNode & LogicMapping:
    - LogicNode: Represents a node in the decision tree where conditions are evaluated. A node evaluates its condition and takes one of its two arms.
    - LogicMapping: The named store the conditions read from, holding the state used in decision-making. Feed it a new value and the next walk moves.

- State:
    - A dictionary containing the variables used for decision-making, such as exposure, working_order, up_prob, etc.

- RootLogicNode:
    - The entry point for the decision tree; its single child is the top of the tree.

- Decision Conditions:
    - Inside each with block, logical conditions are evaluated (e.g., lg.volatility > 0.25, lg.up_prob < -0.1) to determine which action to take.
    - Each with block nests one level deeper; the first block under a node is its true arm, the second its false arm.
    - Actions like LongAction() or ShortAction() are taken based on the conditions.

- Action Handling:
    - LongAction(), ShortAction(), and NoAction() represent different actions you can trigger in the decision tree based on the conditions.
    - An arm left empty is filled with a NoAction when the tree is evaluated.

- Logging:
    - The result of the tree evaluation is logged using the LOGGER object, which outputs to the console.

- Visualization:
    - root.to_html('tree.html') generates a standalone HTML page visualizing the decision tree. Drop the file name and the page is named after the root.

# Features

- Easily define custom decision rules.
- Actionable outcomes like LongAction, ShortAction, and NoAction.
- Log outputs for debugging and tracking.
- Visualize decision paths through HTML export.
- Bake a finished tree once (root.bake()) and let it decide in a hot loop.
- Bake-layer store lookups (`LogicMapping.get` / `set_default`) that keep a missing name distinct from a held-but-empty one.
- CCP-safe invalidation for bake wrappers when their C block is released.
- C diagnostics that name the operation, the cause, and the consequence.
- Slot-stable unlink, replace, and placeholder consolidation.

---

# Contributing

Feel free to fork, submit issues, and create pull requests. Contributions are always welcome!

# License

This project is licensed under the Mozilla Public License 2.0 - see the LICENSE file for details.