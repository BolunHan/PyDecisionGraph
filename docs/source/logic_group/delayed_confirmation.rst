Delayed Confirmation Logic Group
================================

Overview
--------

The `DelayedConfirmationLogicGroup` builds on `SignalLogicGroup` to support
request/response style confirmation patterns. It creates and manages a
`PendingRequest` and exposes helper action node types (`RequestRegistered`,
`RequestDenied`, and `RequestConfirmed`) to bind to that request.

Design summary
--------------

- Each `DelayedConfirmationLogicGroup` owns a `PendingRequest` instance
  (stored in `context['pending_request']`).
- `PendingRequest` encapsulates registration, activation, confirmation,
  and timeout logic for asynchronous user-driven requests.
- Helper action node factories are provided:
  - `register(sig, timeout, rtype)` returns a `RequestRegistered` node
  - `deny()` returns a `RequestDenied` node
  - `confirm(sig)` returns a `RequestConfirmed` node

Usage example
-------------

.. code-block:: python

    from decision_graph.logic_group.pending_request import DelayedConfirmationLogicGroup

    with DelayedConfirmationLogicGroup() as dclg:
        reg_node = dclg.register(sig=1, timeout=10.0)
        # Later, confirming uses the request object bound with the node

API reference
-------------

.. automodule:: decision_graph.logic_group.pending_request
   :members:
   :undoc-members:
   :show-inheritance:

