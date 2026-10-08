Welcome to PyDecisionGraph
==========================

**PyDecisionGraph** builds decision trees as executable graphs. You describe a
decision as nested conditions over named values — *if exposure is zero, and
volatility is high, then go short* — and the library turns that description
into a graph of nodes that can be evaluated, visualized, serialized by design
and baked for a hot loop.

It is written for trading and financial decision-making, where a rule set
changes often, must be reviewable, and must run fast once settled.

A 30-second taste
-----------------

.. code-block:: python

    from decision_graph.decision_tree import LongAction, ShortAction, RootLogicNode, LogicMapping

    state = {"volatility": 0.26, "down_prob": 0.2}

    with LogicMapping(name='book', data=state) as book:
        with RootLogicNode(name='Entry') as root:
            with book.volatility > 0.25:
                with book.down_prob > 0.1:
                    ShortAction()
                with book.down_prob <= 0.1:
                    LongAction()

    print(root())        # the action the tree decided on

The graph is built by nesting ``with`` blocks; the store answers the reads
live, so feeding it a new value moves the next walk. See
:doc:`getting_started/quickstart` for a full walk-through and
:doc:`usage/examples` for worked examples.

How to read these docs
----------------------

The guides below are for humans and for agents alike: paths, commands and
behaviour are stated as they are in this release, so a reader — or a
machine — can act on them directly. The :doc:`usage/architecture` page is the
map of the codebase; :doc:`contributing/development` carries the exact build
and test commands.

.. toctree::
   :maxdepth: 2
   :caption: Getting started

   getting_started/installation
   getting_started/quickstart
   getting_started/concepts

.. toctree::
   :maxdepth: 2
   :caption: Usage

   usage/examples
   usage/architecture
   usage/gallery
   usage/limitations
   webui

.. toctree::
   :maxdepth: 2
   :caption: Reference

   api_reference

.. toctree::
   :maxdepth: 2
   :caption: Contributing

   contributing/development
   contributing/testing
