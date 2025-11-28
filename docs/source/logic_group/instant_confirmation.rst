Instant Confirmation Logic Group
================================

Overview
--------

The `InstantConfirmationLogicGroup` is a small convenience subclass of
`SignalLogicGroup` that provides a confirm API returning action nodes
suitable for immediate confirmation semantics.

Design and usage
----------------

- Constructed with an optional parent `SignalLogicGroup` (defaults to the
  active group via `LGM` when omitted).
- `confirm(sig)` accepts `+1` or `-1` and returns a `LongAction` or
  `ShortAction` respectively. If `LGM.inspection_mode` is set and a
  non-standard signal is passed, a `NoAction` may be returned.

Usage example
-------------

.. code-block:: python

    from decision_graph.logic_group.base import InstantConfirmationLogicGroup

    with InstantConfirmationLogicGroup() as lg:
        action = lg.confirm(1)
        # action is a LongAction (or NoAction depending on context)

API reference
-------------

.. automodule:: decision_graph.logic_group.base
   :members:
   :undoc-members:
   :show-inheritance:

