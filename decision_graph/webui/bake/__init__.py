"""The web UI of the bake layer (``decision_graph.decision_tree.bake``).

The graph it draws is a bake graph — the same node set the bake protocol
prepares for a hot walk — read through the bake surface alone; see ``.app`` for
the one place that surface is named.

A bake graph comes in two states and the page draws both:

- UNBAKED, the way a build leaves it: the breakpoints a split build stopped at
  are still in the graph, and a branch that was reserved and never filled is
  still a placeholder;
- BAKED, the way a walk sees it: the breakpoints are gone (what they carried
  took their place and their arm), the stand-ins are consolidated, and the
  graph is sealed and locked.
"""

import logging

from .. import LOGGER
from decision_graph.decision_tree.bake.c_hierarchy import RootLogicNode
from decision_graph.decision_tree.bake.c_node import LogicNode

LOGGER = LOGGER.getChild('WebUI.Bake')

from . import app
from .app import BakeWebUi


def show(root: LogicNode, with_eval: bool = True, **kwargs):
    _app = BakeWebUi(
        host=kwargs.get('host', "127.0.0.1"),
        port=kwargs.get('port', 5000),
        debug=kwargs.get('debug', False)
    )
    _app.show(
        node=root,
        with_eval=with_eval
    )


def watch(root: RootLogicNode, interval: float = .5, block: bool = False, **kwargs):
    _app = BakeWebUi(
        host=kwargs.get('host', "127.0.0.1"),
        port=kwargs.get('port', 5000),
        debug=kwargs.get('debug', False)
    )
    _app.watch(
        node=root,
        interval=interval,
        block=block
    )


def to_html(root: LogicNode, file_name: str, with_eval: bool = True):
    BakeWebUi.to_html(
        node=root,
        file_name=file_name,
        with_eval=with_eval
    )


def set_logger(logger: logging.Logger):
    global LOGGER
    LOGGER = logger.getChild('WebUI.Bake')
    app.LOGGER = LOGGER
