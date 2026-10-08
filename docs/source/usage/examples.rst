Examples
========

Five runnable examples, each a standalone script under ``docs/examples/``.
Every one of them was executed against this release; the outputs quoted below
are the outputs they produced, verbatim.

A decision over a store — the quick start
-----------------------------------------

Build a rule set, export it, evaluate it. The full walk-through is in
:doc:`../getting_started/quickstart`.

.. literalinclude:: ../../examples/quickstart.py
   :language: python

Output::

    INFO - <LongAction>(sig=1)

The store moves the decision
----------------------------

The case the library is built around: the graph is built **once**, and the
store drives it. Every read answers live, so feeding the store new values is
what moves the next walk — and ``root.eval_path`` records where each walk went.

.. literalinclude:: ../../examples/store_walk.py
   :language: python

Output (three feeds, three different decisions)::

    {'volatility': 0.3, 'down_prob': 0.15, 'up_prob': 0.85} -> <ShortAction>(sig=-1)
      walk: <RootLogicNode>('Store Walk') > <ComparisonExpression>('book.volatility > 0.25') > <ComparisonExpression>('book.down_prob > 0.1') > <ShortAction>(sig=-1)
    {'volatility': 0.3, 'down_prob': 0.05, 'up_prob': 0.85} -> <NoAction>(sig=0)
      walk: <RootLogicNode>('Store Walk') > <ComparisonExpression>('book.volatility > 0.25') > <ComparisonExpression>('book.down_prob > 0.1') > <NoAction>(sig=0)
    {'volatility': 0.1, 'down_prob': 0.15, 'up_prob': 0.85} -> <LongAction>(sig=1)
      walk: <RootLogicNode>('Store Walk') > <ComparisonExpression>('book.volatility > 0.25') > <ComparisonExpression>('book.up_prob > 0.5') > <LongAction>(sig=1)

Expressions and operands
------------------------

Comparisons, ``&``/``|`` combinations and arithmetic between reads each build
an expression **node**. The walk evaluates them where they stand, and the
display text of the node is the expression it evaluates.

.. literalinclude:: ../../examples/expressions.py
   :language: python

Output::

    {'exposure': 2, 'down_prob': 0.15, 'up_prob': 0.85, 'volatility': 0.3} -> <ShortAction>(sig=-1)
      walk: <RootLogicNode>('Expressions') > <ComparisonExpression>('book.exposure > 0') > <LogicalExpression>('book.down_prob > 0.1 & book.volatility > 0.25') > <ShortAction>(sig=-1)
    {'exposure': 2, 'down_prob': 0.05, 'up_prob': 0.85, 'volatility': 0.1} -> <NoAction>(sig=0)
      walk: <RootLogicNode>('Expressions') > <ComparisonExpression>('book.exposure > 0') > <LogicalExpression>('book.down_prob > 0.1 & book.volatility > 0.25') > <NoAction>(sig=0)

A graph assembled by two functions
----------------------------------

A build can stop in one function and be carried on in another: the first half
raises a break, the second half enters the breakpoint it left and continues.
The breakpoint is scaffolding — ``bake()`` takes it down once the graph is
complete, and the continuation takes its place.

.. literalinclude:: ../../examples/split_build.py
   :language: python

Output::

    bake: OK · 11 nodes · 4 levels deep
    decision: <LongAction('LongAction')>
    walk: <RootLogicNode>('Split Build') > <BinaryExpression>('book.exposure > 0') > <BinaryExpression>('book.up_prob > 0.5') > <LongAction('LongAction')>

Bake once, decide in a hot loop
-------------------------------

The bake layer's shape: build, ``bake()`` once — verify, lock, prepare — then
evaluate as often as needed. The store stays live after the bake; the shape
does not change.

.. literalinclude:: ../../examples/bake_hot_loop.py
   :language: python

Output::

    bake: OK · 14 nodes · 3 levels deep · locked 14
    decision: <LongAction('LongAction')>
    walk: <RootLogicNode>('Hot Loop') > <BinaryExpression('book.exposure > 0')> > <BinaryExpression('book.up_prob > 0.5')> > <LongAction('LongAction')>
    refed decision: <ShortAction('ShortAction')>

Running the examples yourself
-----------------------------

With the package installed::

    python docs/examples/quickstart.py

From a source checkout without installing, put the repository on the import
path::

    PYTHONPATH=. python docs/examples/quickstart.py
