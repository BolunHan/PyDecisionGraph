# Python API Summary

## Overview
This module is a Cython-accelerated decision tree engine with a Python fallback. The Cython API is in `decision_graph.decision_tree.capi`, and the Python API is in `decision_graph.decision_tree.native`. Both provide nearly identical functionality, except for some Python limitations and performance differences.

- By default, the package uses the Cython API if available, otherwise falls back to Python.
- API reference is auto-generated from `decision_graph/decision_tree/capi/*.pyi` files.

## Differences between CAPI and Native API
- `LogicExpression.eq` is overridden in native API, returning a boolean instead of a new LogicExpression.
- Internal stacks in `LogicNode`, `LogicGroupManager` are exposed in Python due to language limitations.
- `**kwargs` guard is removed for LogicExpress base class in Python.
- `GetterExpression` and `GetterNestedExpression` are aliases of `AttrExpression` and `AttrNestedExpression` in Python, due to relaxed type checking.

See the API reference for details.
