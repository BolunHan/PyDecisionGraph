Quick Start
===========

This is the same script the project README shows, run end to end. It builds a
small trading rule, exports it to HTML, and evaluates it.

.. literalinclude:: ../../examples/quickstart.py
   :language: python

Running it prints (the log line comes from the package logger)::

    <LongAction>(sig=1)

and writes ``tree.html`` next to the working directory — a self-contained page
you can open in a browser, with no server and no network.

What just happened
------------------

#. **The store was entered first.**
   ``LogicMapping(name='Root', data=state)`` is the *store*: the named values
   the conditions read from. Entering it makes it the store the build runs
   inside — see :doc:`concepts` for why that matters.

#. **The tree was built by nesting.** Each ``with`` block opens a node. Under
   a node, the **first** nested block is its *true arm* and the **second** is
   its *false arm*. So the script says:

   - *if* ``exposure == 0`` (it is),
   - *then* check ``volatility > 0.25`` (0.26, so yes),
   - *then* check ``down_prob > 0.1`` (0.2, so yes) → ``LongAction()``;
   - the `false` arm of the volatility check would have asked
     ``up_prob < -0.1``.

#. **The walk answered** with the action the arms led to — ``LongAction`` —
   and recorded how it got there. After a walk, ``root.eval_path`` holds the
   path: the root first, the landed-on leaf last.

Feeding it something else moves it
----------------------------------

Every read answers *live* out of the store, so the same graph decides
differently as the store changes — no rebuild, no re-parse:

.. code-block:: python

    state['volatility'] = 0.10     # quiet market
    print(root())                  # <NoAction>(sig=0) - the walk went the other way

:doc:`../usage/examples` shows this store-driven walk measured across several
feeds, alongside expressions, split builds and the bake layer.

Visualize it
------------

Every layer has three doors — ``show``, ``watch`` and ``to_html`` — both as
module functions and as methods on that layer's root node:

.. code-block:: python

    root.to_html('tree.html')      # a standalone page
    root.to_html()                 # ... named after the root's repr
    root.show()                    # a local server + your browser
    root.watch()                   # stream activation changes over SSE

The page shows the graph drawn from the very objects you built. See
:doc:`../webui` for what the viewer draws, and :doc:`../usage/gallery` for
snapshots of each of the three viewers.
